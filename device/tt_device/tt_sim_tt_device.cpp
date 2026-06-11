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
#include "simulation/simulation_socket.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/pci_ids.h"
#include "umd/device/pcie/tt_sim_tlb_handle.hpp"
#include "umd/device/pcie/tt_sim_tlb_window.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
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
    return TTSimTTDevice::create_for_chip(simulator_directory, /* chip_id= */ static_cast<ChipId>(0), copy_sim_binary);
}

std::unique_ptr<TTSimTTDevice> TTSimTTDevice::create_for_chip(
    const std::filesystem::path& simulator_directory, ChipId chip_id, bool copy_sim_binary) {
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
    return std::make_unique<TTSimTTDevice>(simulator_directory, soc_descriptor, chip_id, copy_sim_binary, 0);
}

TTSimTTDevice::TTSimTTDevice(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    bool copy_sim_binary,
    int num_host_mem_channels) :
    // Pass chip_id to the communicator. If the loaded .so supports the multichip
    // multichip ABI (libttsim_create_device_by_id + libttsim_select_device_by_id),
    // the communicator will auto-detect at initialize() time and switch to
    // shared-dlopen mode regardless of copy_sim_binary.
    communicator_(
        std::make_unique<TTSimCommunicator>(simulator_directory, copy_sim_binary, static_cast<uint32_t>(chip_id))),
    simulator_directory_(simulator_directory),
    chip_id_(chip_id),
    sysmem_manager_(std::make_unique<SimulationSysmemManager>(num_host_mem_channels, soc_descriptor.arch)) {
    set_soc_descriptor(soc_descriptor);
    // Populate the base-class arch field from the soc descriptor. TTSim does not go through
    // init_tt_device() (no PCI probe), so without this arch stays tt::ARCH::Invalid and downstream
    // consumers (e.g. tt-exalens constructing a SocDescriptor from the device) see the wrong arch.
    arch = soc_descriptor.arch;
    architecture_impl_ = architecture_implementation::create(soc_descriptor.arch);
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

    tlb_allocator_ = std::make_shared<SimulationTlbAllocator>(bar0_base, architecture_impl_.get());

    // Allocate the cached default TLB window. Quasar has no real TLBs; the communicator handles
    // all I/O underneath. The 4GB size for Quasar is a dummy value -- it just needs to be large
    // enough so that TlbWindow::validate doesn't reject any valid access (size 0 would cause
    // division by zero in TLB handle configure).
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            cached_tlb_window_ = TTSimTTDevice::get_io_window({}, TlbMapping::WC, SIZE_2MB);
            break;
        case tt::ARCH::WORMHOLE_B0:
            cached_tlb_window_ = TTSimTTDevice::get_io_window({}, TlbMapping::WC, SIZE_16MB);
            break;
        case tt::ARCH::QUASAR:
            cached_tlb_window_ = TTSimTTDevice::get_io_window({}, TlbMapping::WC, SIZE_4GB);
            break;
        default:
            log_debug(
                LogUMD,
                "Architecture {} does not support TLB allocation, leaving cached_tlb_window_ null.",
                tt::arch_to_str(arch));
            break;
    }
}

std::unique_ptr<TlbWindow> TTSimTTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    int tlb_index = tlb_allocator_->allocate_tlb_index(size);
    if (tlb_index == -1) {
        UMD_THROW(error::RuntimeError, "No available TLB of requested size.");
    }
    // QUASAR bypasses the bitmap allocator (pools are empty by design); pass the requested
    // size through, since get_tlb_size_from_index has no pool to look up for the bypass index.
    size_t actual_size = (get_arch() == tt::ARCH::QUASAR) ? size : tlb_allocator_->get_tlb_size_from_index(tlb_index);
    auto handle = TTSimTlbHandle::create(tlb_allocator_, communicator_.get(), tlb_index, actual_size, mapping);
    return std::make_unique<TTSimTlbWindow>(std::move(handle), communicator_.get(), config);
}

TTSimTTDevice::~TTSimTTDevice() {
    // Stop serving (and remove the socket) before tearing the backend down.
    socket_.reset();
    communicator_->shutdown();
}

void TTSimTTDevice::adopt_socket(std::unique_ptr<SimulationSocket> socket) { socket_ = std::move(socket); }

void TTSimTTDevice::start_device() {}

void TTSimTTDevice::close_device() {
    communicator_->mark_closed();
    communicator_->shutdown();
}

void TTSimTTDevice::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    if (communicator_->is_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (sim_dram_teleport_enabled()) {
        if (get_soc_descriptor().is_core_of_type(core, CoreType::DRAM, CoordSystem::TRANSLATED)) {
            if (communicator_->dram_write_bytes(core.x, core.y, addr, mem_ptr, size)) {
                return;
            }
            communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
            return;
        }
    }
    if (get_arch() != tt::ARCH::QUASAR && cached_tlb_window_) {
        cached_tlb_window_->write_block_reconfigure(mem_ptr, core, addr, size, get_selected_noc_id());
    } else {
        communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
    }
}

void TTSimTTDevice::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    if (communicator_->is_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (sim_dram_teleport_enabled()) {
        if (get_soc_descriptor().is_core_of_type(core, CoreType::DRAM, CoordSystem::TRANSLATED)) {
            if (!communicator_->dram_read_bytes(core.x, core.y, addr, mem_ptr, size)) {
                communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
            }
            communicator_->advance_clock(10);
            return;
        }
    }
    if (get_arch() != tt::ARCH::QUASAR && cached_tlb_window_) {
        cached_tlb_window_->read_block_reconfigure(mem_ptr, core, addr, size, get_selected_noc_id());
    } else {
        communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
    }
    // Ideally we would not auto-clock on reads at all, but some clocking is required to avoid hangs
    // in the absence of an API reliably called from all spin loops polling the device
    communicator_->advance_clock(1);
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

void TTSimTTDevice::dma_d2h(void* dst, uint32_t src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_h2d(uint32_t dst, const void* src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC CSM access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC CSM access is not supported in TTSim simulation device.");
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

uint32_t TTSimTTDevice::get_clock() {
    UMD_THROW(error::RuntimeError, "Getting clock is not supported in TTSim simulation device.");
}

uint32_t TTSimTTDevice::get_min_clock_freq() {
    UMD_THROW(error::RuntimeError, "Getting minimum clock frequency is not supported in TTSim simulation device.");
}

bool TTSimTTDevice::get_noc_translation_enabled() {
    // TTSim operates on logical/virtual coordinates end-to-end; NOC translation is never applied.
    return false;
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

void TTSimTTDevice::dma_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    UMD_THROW(error::RuntimeError, "DMA multicast write not supported for TTSim simulation device.");
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

void TTSimTTDevice::retrain_dram_core(const uint32_t dram_channel) {
    UMD_THROW(error::RuntimeError, "DRAM retraining is not supported in TTSim device.");
}

void TTSimTTDevice::noc_multicast_write(
    const void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    multicast_write_via_unicast(src, size, core_start, core_end, addr);
}

void TTSimTTDevice::noc_multicast_write(const void* src, size_t size, uint64_t addr) {
    UMD_THROW(error::RuntimeError, "NOC multicast write is not supported in TTSim simulation device.");
}

}  // namespace tt::umd
