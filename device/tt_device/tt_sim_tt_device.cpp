// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_sim_tt_device.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "tt-kmd-lib/pci_ids.h"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/tt_sim_tlb_handle.hpp"
#include "umd/device/pcie/tt_sim_tlb_window.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<TTSimTTDevice>(), "TTSimChip must be non-abstract.");

namespace {

bool sim_dram_teleport_enabled() {
    // Cache the result since this is called on every device read/write.
    static const bool enabled = [] {
        const char* env = std::getenv("TT_SIMULATOR_DRAM_TELEPORT");
        if (env == nullptr) {
            return false;
        }
        std::string_view value(env);
        return value == "1" || value == "true" || value == "TRUE" || value == "on" || value == "ON";
    }();
    return enabled;
}

}  // namespace

std::unique_ptr<TTSimTTDevice> TTSimTTDevice::create(
    const std::filesystem::path& simulator_directory, int num_host_mem_channels, bool copy_sim_binary) {
    return TTSimTTDevice::create_for_chip(
        simulator_directory, /* chip_id= */ static_cast<ChipId>(0), num_host_mem_channels, copy_sim_binary);
}

std::unique_ptr<TTSimTTDevice> TTSimTTDevice::create_for_chip(
    const std::filesystem::path& simulator_directory, ChipId chip_id, int num_host_mem_channels, bool copy_sim_binary) {
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    tt::ARCH arch = SocDescriptor::get_arch_from_soc_descriptor_path(soc_desc_path);
    ChipInfo chip_info{};
    if (arch == tt::ARCH::BLACKHOLE) {
        // We need to set this default harvesting mask for Blackhole so we could create SocDescriptor.
        // We have the same code in creating mock cluster descriptor, but this code is supposed to be used.
        // without creating ClusterDescriptor, so we need to add it here as well.
        chip_info.harvesting_masks.eth_harvesting_mask = 0x120;
    }
    SocDescriptor soc_descriptor = SocDescriptor(std::make_shared<SocArchDescriptor>(soc_desc_path), chip_info);
    return std::make_unique<TTSimTTDevice>(
        simulator_directory, soc_descriptor, chip_id, copy_sim_binary, num_host_mem_channels);
}

std::unique_ptr<TTSimTTDevice> TTSimTTDevice::create_client(
    const std::filesystem::path& simulator_directory, ChipId chip_id, std::unique_ptr<SimulationClient> client) {
    UMD_ASSERT(
        client != nullptr, error::RuntimeError, "Client-mode TTSimTTDevice requires a non-null SimulationClient.");
    // The SoC descriptor is read straight from the local simulator build -- the same files the
    // host used -- so the client can describe the device without loading or running the .so.
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    tt::ARCH arch = SocDescriptor::get_arch_from_soc_descriptor_path(soc_desc_path);
    ChipInfo chip_info{};
    if (arch == tt::ARCH::BLACKHOLE) {
        // Same default ETH harvesting mask as create_for_chip(): BH SocDescriptor construction
        // rejects an empty mask, so apply it here too. Keep in sync with create_for_chip().
        chip_info.harvesting_masks.eth_harvesting_mask = 0x120;
    }
    SocDescriptor soc_descriptor = SocDescriptor(std::make_shared<SocArchDescriptor>(soc_desc_path), chip_info);
    // make_unique can't reach the private client-mode constructor; this static factory can via new.
    return std::unique_ptr<TTSimTTDevice>(new TTSimTTDevice(soc_descriptor, chip_id, std::move(client)));
}

TTSimTTDevice::TTSimTTDevice(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    bool copy_sim_binary,
    int num_host_mem_channels) :
    SimulationTTDevice(
        simulator_directory, std::make_unique<SimulationSysmemManager>(num_host_mem_channels, soc_descriptor.arch)),
    // Pass chip_id to the communicator. If the loaded .so supports the multichip
    // multichip ABI (libttsim_create_device_by_id + libttsim_select_device_by_id),
    // the communicator will auto-detect at initialize() time and switch to
    // shared-dlopen mode regardless of copy_sim_binary.
    communicator_(
        std::make_unique<TTSimCommunicator>(simulator_directory, copy_sim_binary, static_cast<uint32_t>(chip_id))),
    chip_id_(chip_id) {
    set_soc_descriptor(soc_descriptor);
    // Populate the base-class arch field from the soc descriptor. TTSim does not go through
    // init_tt_device() (no PCI probe), so without this arch stays tt::ARCH::Invalid and downstream
    // consumers (e.g. tt-exalens constructing a SocDescriptor from the device) see the wrong arch.
    arch = soc_descriptor.arch;
    architecture_impl_ = architecture_implementation::create(soc_descriptor.arch);
    // Host/local mode: the lifecycle drives the in-process .so backend (the communicator).
    setup_ = [this] { initialize_backend(); };
    teardown_ = [this] { communicator_->shutdown(); };
    setup_();
}

void TTSimTTDevice::initialize_backend() {
    communicator_->initialize();
    initialize_sysmem_functions();
    communicator_->start_sim();
    // Read the PCI ID (first 32 bits of PCI config space).
    uint32_t pci_id = communicator_->pci_config_read32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = communicator_->pci_config_read32(0, 0) >> 16;
    log_info(
        tt::LogEmulationDriver,
        "TTSimTTDevice chip_id={} PCI vendor_id=0x{:x} device_id=0x{:x}",
        chip_id_,
        vendor_id,
        libttsim_pci_device_id);
    UMD_ASSERT(vendor_id == 0x1E52, error::RuntimeError, "Unexpected PCI vendor ID.");

    if ((libttsim_pci_device_id == TT_WORMHOLE_PCI_DEVICE_ID) ||
        (libttsim_pci_device_id == TT_BLACKHOLE_PCI_DEVICE_ID)) {
        // Compute physical address of BAR0 from PCI config registers.
        bar0_base = communicator_->pci_config_read32(0, 0x10);
        bar0_base |= uint64_t(communicator_->pci_config_read32(0, 0x14)) << 32;
        bar0_base &= ~15ull;  // ignore attributes, just obtain the physical address

        // BAR4 is a 64-bit memory BAR; its base address is split across two PCI config
        // registers -- 0x20 holds the low 32 bits, 0x24 holds the high 32 bits. The low 4 bits
        // of the low register encode BAR attributes (memory vs IO, prefetchable, 64-bit width)
        // and are masked off to leave the physical address.
        bar4_base = communicator_->pci_config_read32(0, 0x20);
        bar4_base |= uint64_t(communicator_->pci_config_read32(0, 0x24)) << 32;
        bar4_base &= ~15ull;

        if (libttsim_pci_device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
            tlb_region_size_ = 16 * 1024 * 1024;
        } else {
            tlb_region_size_ = 2 * 1024 * 1024;
        }
    }

    init_tlb_allocator(bar0_base);
    setup_cached_tlb_window();
}

TTSimTTDevice::TTSimTTDevice(
    const SocDescriptor& soc_descriptor, ChipId chip_id, std::unique_ptr<SimulationClient> client) :
    client_(std::move(client)), chip_id_(chip_id) {
    set_soc_descriptor(soc_descriptor);
    arch = soc_descriptor.arch;
    architecture_impl_ = architecture_implementation::create(soc_descriptor.arch);

    // Client mode: the lifecycle drives the remote host over the socket. read/write are not wired
    // here -- the SimulationClient has no device I/O yet -- so those throw until the API grows.
    // create_client() has already validated that client_ is non-null.
    setup_ = [this] { client_->attach(); };
    teardown_ = [this] { client_->detach(); };
    setup_();
}

std::unique_ptr<TlbWindow> TTSimTTDevice::create_tlb_window(
    int tlb_index, size_t size, TlbMapping mapping, tlb_data config) {
    auto handle = TTSimTlbHandle::create(tlb_allocator_, communicator_.get(), tlb_index, size, mapping);
    return std::make_unique<TTSimTlbWindow>(std::move(handle), communicator_.get(), config);
}

TTSimTTDevice::~TTSimTTDevice() {
    // Stop serving (and remove the socket) before tearing the backend down.
    socket_.reset();
    // teardown_ is communicator_->shutdown() (host) or client_->detach() (client). The
    // destructor is implicitly noexcept, so this is best-effort -- a throw here would call
    // std::terminate during unwinding.
    if (teardown_) {
        try {
            teardown_();
        } catch (const std::exception& e) {
            log_warning(tt::LogEmulationDriver, "TTSimTTDevice teardown failed: {}", e.what());
        }
    }
}

void TTSimTTDevice::start_device() {}

void TTSimTTDevice::close_device() {
    // Client mode has no local backend (communicator_) to close; the host session is dropped by
    // client_->detach() in the destructor (idempotent), keeping teardown symmetric with
    // RtlSimulationTTDevice, which has no close_device() override.
    if (client_) {
        return;
    }
    communicator_->mark_closed();
    communicator_->shutdown();
}

void TTSimTTDevice::tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) {
    communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
}

void TTSimTTDevice::tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) {
    communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
}

bool TTSimTTDevice::is_device_closed() { return communicator_->is_closed(); }

bool TTSimTTDevice::should_use_cached_tlb_window() {
    return get_arch() != tt::ARCH::QUASAR && cached_tlb_window_ != nullptr;
}

void TTSimTTDevice::after_read() {
    // Ideally we would not auto-clock on reads at all, but some clocking is required to avoid hangs
    // in the absence of an API reliably called from all spin loops polling the device.
    communicator_->advance_clock(1);
}

bool TTSimTTDevice::handle_special_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    return special_dram_write(mem_ptr, core, addr, size);
}

bool TTSimTTDevice::handle_special_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    return special_dram_read(mem_ptr, core, addr, size);
}

bool TTSimTTDevice::special_dram_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    if (!sim_dram_teleport_enabled()) {
        return false;
    }
    if (!get_soc_descriptor().is_core_of_type(core, CoreType::DRAM, CoordSystem::TRANSLATED)) {
        return false;
    }
    if (communicator_->dram_write_bytes(core.x, core.y, addr, mem_ptr, size)) {
        return true;
    }
    communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
    return true;
}

bool TTSimTTDevice::special_dram_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    if (!sim_dram_teleport_enabled()) {
        return false;
    }
    if (!get_soc_descriptor().is_core_of_type(core, CoreType::DRAM, CoordSystem::TRANSLATED)) {
        return false;
    }
    if (!communicator_->dram_read_bytes(core.x, core.y, addr, mem_ptr, size)) {
        communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
    }
    // Side effect: this path advances the simulation clock by 10 cycles (rather than the single
    // cycle after_read() applies), and it returns before after_read() would otherwise run.
    communicator_->advance_clock(10);
    return true;
}

void TTSimTTDevice::assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value &=
            ~(uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value |= soft_reset_update;
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    }
}

void TTSimTTDevice::deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);

    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value |=
            (uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value &= ~soft_reset_update;
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    }
}

void TTSimTTDevice::advance_device_execution() {
    // Simulator clocking is driven synchronously from the calling thread to keep the simulation
    // deterministic. A background clock thread would race with reads/writes and produce
    // non-reproducible runs, so we advance the clock here instead.
    // Ideally we would not auto-clock on reads at all, but some clocking is required to avoid
    // hangs in the absence of an API reliably called from all spin loops polling the device.
    if (communicator_) {
        communicator_->advance_clock(1);
    }
}

void TTSimTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) {
    UMD_THROW(error::RuntimeError, "Waiting for ARC core start is not supported in TTSim simulation device.");
}

std::chrono::milliseconds TTSimTTDevice::wait_eth_core_training(
    const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms) {
    UMD_THROW(error::RuntimeError, "Waiting for ETH core training is not supported in TTSim simulation device.");
}

EthTrainingStatus TTSimTTDevice::read_eth_core_training_status(tt_xy_pair eth_core) {
    UMD_THROW(error::RuntimeError, "Reading ETH core training status is not supported in TTSim simulation device.");
}

ChipInfo TTSimTTDevice::get_chip_info() {
    // No firmware_info_provider on the simulator; mirror the defaults used inside
    // TTSimTTDevice::create(). BH SocDescriptor construction rejects an empty eth_harvesting_mask
    // ("Exactly 2 or 14 ETH cores should be harvested on full Blackhole"), so apply the same 0x120
    // default here. Keep in sync with create() above.
    ChipInfo chip_info{};
    if (arch == tt::ARCH::BLACKHOLE) {
        chip_info.harvesting_masks.eth_harvesting_mask = 0x120;
    }
    return chip_info;
}

void TTSimTTDevice::initialize_sysmem_functions() {
    communicator_->set_pcie_dma_mem_callbacks(
        [this](uint64_t a, void* p, uint32_t s) { pci_dma_read_bytes(a, p, s); },
        [this](uint64_t a, const void* p, uint32_t s) { pci_dma_write_bytes(a, p, s); });
}

void TTSimTTDevice::pci_dma_read_bytes(uint64_t paddr, void* p, uint32_t size) {
    // craq-sim calls translate_pci_dma_addr() before invoking this callback,
    // which subtracts pcie_base from the NOC address.  So paddr here is an
    // OFFSET from pcie_base (not an absolute address).  Two backing stores:
    //
    //  1. Mapped-buffer arena: allocate_sysmem_buffer / map_sysmem_buffer
    //     assign synthetic IOVAs above the hugepage region.  The registry
    //     keys buffers by their absolute device IO address (pcie_base + offset),
    //     so convert paddr before the lookup.
    //
    //  2. Hugepage arena: traditional channel-stride layout.  paddr is already
    //     the within-hugepage-space offset, so decompose directly into
    //     (channel, within-channel offset) for read_from_sysmem.
    auto* sim_mgr = static_cast<SimulationSysmemManager*>(sysmem_manager_.get());
    const uint64_t pcie_base = sim_mgr->get_pcie_base();
    if (sim_mgr->read_mapped_buffer(pcie_base + paddr, p, size)) {
        return;
    }
    const uint16_t channel = static_cast<uint16_t>(paddr / (1ULL << 30));
    sim_mgr->read_from_sysmem(channel, p, paddr % (1ULL << 30), size);
}

void TTSimTTDevice::pci_dma_write_bytes(uint64_t paddr, const void* p, uint32_t size) {
    // See pci_dma_read_bytes for the offset-vs-absolute explanation.
    auto* sim_mgr = static_cast<SimulationSysmemManager*>(sysmem_manager_.get());
    const uint64_t pcie_base = sim_mgr->get_pcie_base();
    if (sim_mgr->write_mapped_buffer(pcie_base + paddr, p, size)) {
        return;
    }
    const uint16_t channel = static_cast<uint16_t>(paddr / (1ULL << 30));
    sim_mgr->write_to_sysmem(channel, p, paddr % (1ULL << 30), size);
}

}  // namespace tt::umd
