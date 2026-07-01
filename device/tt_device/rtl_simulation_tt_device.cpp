// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <utility>

#include "noc_access.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/rtl_sim_tlb_handle.hpp"
#include "umd/device/pcie/rtl_sim_tlb_window.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/simulation/rtl_sim_communicator.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<RtlSimulationTTDevice>(), "RtlSimulationTTDevice must be non-abstract.");

// Array of DM RiscType values for iteration.
static constexpr std::array<RiscType, 8> RISC_TYPES_DMS = {
    RiscType::DM0,
    RiscType::DM1,
    RiscType::DM2,
    RiscType::DM3,
    RiscType::DM4,
    RiscType::DM5,
    RiscType::DM6,
    RiscType::DM7};

static constexpr ChipId DEFAULT_CHIP_ID = 0;

namespace {
void validate_noc_for_arch(NocId noc_id, tt::ARCH arch) {
    if (noc_id == NocId::SYSTEM_NOC && arch != tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "System NOC is only supported on Grendel (Quasar) architecture.");
    }
    if (noc_id == NocId::NOC1 && arch == tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "NOC1 is not supported on Grendel (Quasar) architecture.");
    }
}
}  // namespace

std::unique_ptr<RtlSimulationTTDevice> RtlSimulationTTDevice::create(
    const std::filesystem::path& simulator_directory, int num_host_mem_channels) {
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    SocDescriptor soc_descriptor = SocDescriptor(std::make_shared<SocArchDescriptor>(soc_desc_path));
    return std::make_unique<RtlSimulationTTDevice>(
        simulator_directory, soc_descriptor, DEFAULT_CHIP_ID, num_host_mem_channels);
}

std::unique_ptr<RtlSimulationTTDevice> RtlSimulationTTDevice::create_client(
    const std::filesystem::path& simulator_directory, ChipId chip_id, std::unique_ptr<SimulationClient> client) {
    UMD_ASSERT(
        client != nullptr,
        error::RuntimeError,
        "Client-mode RtlSimulationTTDevice requires a non-null SimulationClient.");
    // The SoC descriptor is read straight from the local simulator build -- the same files the
    // host used -- so the client can describe the device without loading or running a simulator.
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    SocDescriptor soc_descriptor = SocDescriptor(std::make_shared<SocArchDescriptor>(soc_desc_path));
    // make_unique can't reach the private client-mode constructor; this static factory can via new.
    return std::unique_ptr<RtlSimulationTTDevice>(
        new RtlSimulationTTDevice(soc_descriptor, chip_id, std::move(client)));
}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    communicator_(std::make_unique<RtlSimCommunicator>(simulator_directory)),
    simulator_directory_(simulator_directory),
    sysmem_manager_(std::make_unique<SimulationSysmemManager>(num_host_mem_channels, soc_descriptor.arch)) {
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation TTDevice");
    set_soc_descriptor(soc_descriptor);
    architecture_impl_ = architecture_implementation::create(get_soc_descriptor().arch);
    arch = get_soc_descriptor().arch;

    // Host/local mode: the lifecycle drives the in-process RTL backend (the communicator).
    setup_ = [this, num_host_mem_channels] { initialize_backend(num_host_mem_channels); };
    teardown_ = [this] { communicator_->shutdown(); };
    setup_();
}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const SocDescriptor& soc_descriptor, ChipId chip_id, std::unique_ptr<SimulationClient> client) :
    client_(std::move(client)) {
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

void RtlSimulationTTDevice::initialize_backend(int num_host_mem_channels) {
    // Register sysmem callbacks so the simulator can read/write host memory.
    if (num_host_mem_channels > 0) {
        SimulationSysmemManager* mgr = sysmem_manager_.get();
        size_t num_channels = mgr->get_num_host_mem_channels();
        communicator_->set_ram_callbacks(
            // Write callback: simulator writes data into host sysmem.
            [mgr, num_channels](uint64_t address, const void* data, uint32_t size) {
                uint64_t pcie_base = mgr->get_pcie_base();
                UMD_ASSERT(address >= pcie_base, error::RuntimeError, "RAM callback address underflow.");
                uint64_t offset = address - pcie_base;
                uint16_t channel = static_cast<uint16_t>(offset / (1ULL << 30));
                UMD_ASSERT(channel < num_channels, error::RuntimeError, "RAM callback channel out of range.");
                uint64_t offset_in_channel = offset % (1ULL << 30);
                mgr->write_to_sysmem(channel, data, offset_in_channel, size);
            },
            // Read callback: simulator reads data from host sysmem.
            [mgr, num_channels](uint64_t address, void* data_out, uint32_t size) {
                uint64_t pcie_base = mgr->get_pcie_base();
                UMD_ASSERT(address >= pcie_base, error::RuntimeError, "RAM callback address underflow.");
                uint64_t offset = address - pcie_base;
                uint16_t channel = static_cast<uint16_t>(offset / (1ULL << 30));
                UMD_ASSERT(channel < num_channels, error::RuntimeError, "RAM callback channel out of range.");
                uint64_t offset_in_channel = offset % (1ULL << 30);
                mgr->read_from_sysmem(channel, data_out, offset_in_channel, size);
            });
    }

    communicator_->initialize();

    tlb_allocator_ = std::make_shared<SimulationTlbAllocator>(/*bar0_base=*/0, architecture_impl_.get());

    // Allocate the cached default TLB window. Quasar has no real TLBs; the communicator handles
    // all I/O underneath. The 4GB size for Quasar is a dummy value — it just needs to be large
    // enough so that TlbWindow::validate doesn't reject any valid access (size 0 would cause
    // division by zero in RtlSimTlbHandle::configure).
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            cached_tlb_window_ = RtlSimulationTTDevice::get_io_window({}, TlbMapping::WC, SIZE_2MB);
            break;
        case tt::ARCH::WORMHOLE_B0:
            cached_tlb_window_ = RtlSimulationTTDevice::get_io_window({}, TlbMapping::WC, SIZE_16MB);
            break;
        case tt::ARCH::QUASAR:
            cached_tlb_window_ = RtlSimulationTTDevice::get_io_window({}, TlbMapping::WC, SIZE_4GB);
            break;
        default:
            log_debug(
                LogUMD,
                "Architecture {} does not support TLB allocation, leaving cached_tlb_window_ null.",
                tt::arch_to_str(arch));
            break;
    }
}

std::unique_ptr<TlbWindow> RtlSimulationTTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    if (client_) {
        // Client mode runs no backend, so tlb_allocator_/communicator_ are never constructed. Fail
        // loudly instead of dereferencing them; client-mode device I/O is not available yet.
        UMD_THROW(error::RuntimeError, "Client-mode RtlSimulationTTDevice does not support TLB windows yet.");
    }
    int tlb_index = tlb_allocator_->allocate_tlb_index(size);
    if (tlb_index == -1) {
        UMD_THROW(error::RuntimeError, "No available TLB of requested size.");
    }
    // QUASAR bypasses the bitmap allocator (pools are empty by design); pass the requested
    // size through, since get_tlb_size_from_index has no pool to look up for the bypass index.
    size_t actual_size = (get_arch() == tt::ARCH::QUASAR) ? size : tlb_allocator_->get_tlb_size_from_index(tlb_index);
    auto handle = RtlSimTlbHandle::create(tlb_allocator_, tlb_index, actual_size, mapping);
    return std::make_unique<RtlSimTlbWindow>(std::move(handle), communicator_.get(), config);
}

RtlSimulationTTDevice::~RtlSimulationTTDevice() {
    // Stop serving (and remove the socket) before tearing the backend down.
    socket_.reset();
    // teardown_ is communicator_->shutdown() (host) or client_->detach() (client). The
    // destructor is implicitly noexcept, so this is best-effort.
    if (teardown_) {
        try {
            teardown_();
        } catch (const std::exception& e) {
            log_warning(tt::LogEmulationDriver, "RtlSimulationTTDevice teardown failed: {}", e.what());
        }
    }
}

void RtlSimulationTTDevice::adopt_socket(std::unique_ptr<SimulationServerSocket> socket) {
    socket_ = std::move(socket);
}

void RtlSimulationTTDevice::write_to_device(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    if (client_) {
        UMD_THROW(
            error::RuntimeError,
            "Client-mode RtlSimulationTTDevice device I/O is not available yet (SimulationClient has no "
            "read/write).");
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    log_debug(
        tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, addr, translated_core.str());

    NocId selected_noc_id = get_selected_noc_id();
    validate_noc_for_arch(selected_noc_id, get_soc_descriptor().arch);

    if (selected_noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_write_bytes(translated_core.x, translated_core.y, addr, mem_ptr, size);
        return;
    }

    if (cached_tlb_window_) {
        cached_tlb_window_->write_block_reconfigure(mem_ptr, translated_core, addr, size, selected_noc_id);
    } else {
        communicator_->tile_write_bytes(translated_core.x, translated_core.y, addr, mem_ptr, size);
    }
}

void RtlSimulationTTDevice::read_from_device(void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    if (client_) {
        UMD_THROW(
            error::RuntimeError,
            "Client-mode RtlSimulationTTDevice device I/O is not available yet (SimulationClient has no "
            "read/write).");
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);

    NocId selected_noc_id = get_selected_noc_id();
    validate_noc_for_arch(selected_noc_id, get_soc_descriptor().arch);

    if (selected_noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_read_bytes(translated_core.x, translated_core.y, addr, mem_ptr, size);
        return;
    }

    if (cached_tlb_window_) {
        cached_tlb_window_->read_block_reconfigure(mem_ptr, translated_core, addr, size, selected_noc_id);
    } else {
        communicator_->tile_read_bytes(translated_core.x, translated_core.y, addr, mem_ptr, size);
    }
}

void RtlSimulationTTDevice::assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}.", selected_riscs);
    // If the architecture is Quasar, a special case is needed to control the NEO Data Movement cores.
    if (get_soc_descriptor().arch == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL) {
            communicator_->all_tensix_reset_assert(core.x, core.y);
            communicator_->all_neo_dms_reset_assert(core.x, core.y);
            communicator_->all_neo_dms_uncore_reset_assert();
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            communicator_->all_neo_dms_reset_assert(core.x, core.y);
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS_UNCORE) {
            communicator_->all_neo_dms_uncore_reset_assert();
            return;
        }
        if ((selected_riscs & RiscType::NEO_DM_UNCORE) != RiscType::NONE) {
            communicator_->neo_dm_uncore_reset_assert(core.x, core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_assert(core.x, core.y, i);
            }
        }
    }

    if (get_soc_descriptor().arch != tt::ARCH::QUASAR ||
        (selected_riscs & RiscType::ALL_NEO_TRISCS) != RiscType::NONE) {
        // In case of Wormhole and Blackhole, we don't check which cores are selected, we just assert all tensix cores.
        // So the functionality is if we called with RiscType::ALL_TENSIX or RiscType::ALL.
        // In case of Quasar, this won't assert the NEO Data Movement cores, but will assert the Tensix cores.
        // For simplicity, we don't check and try to list all the combinations of selected_riscs arguments, we just
        // always call this command as if reset for all was requested.
        communicator_->all_tensix_reset_assert(core.x, core.y);
    }
}

void RtlSimulationTTDevice::deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    // See the comment in assert_risc_reset for more details.
    if (get_soc_descriptor().arch == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL) {
            communicator_->all_neo_dms_uncore_reset_deassert();
            communicator_->all_neo_dms_reset_deassert(core.x, core.y);
            communicator_->all_tensix_reset_deassert(core.x, core.y);
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            communicator_->all_neo_dms_reset_deassert(core.x, core.y);
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS_UNCORE) {
            communicator_->all_neo_dms_uncore_reset_deassert();
            return;
        }
        if ((selected_riscs & RiscType::NEO_DM_UNCORE) != RiscType::NONE) {
            communicator_->neo_dm_uncore_reset_deassert(core.x, core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_deassert(core.x, core.y, i);
            }
        }
    }

    if (get_soc_descriptor().arch != tt::ARCH::QUASAR ||
        (selected_riscs & RiscType::ALL_NEO_TRISCS) != RiscType::NONE) {
        // See the comment in assert_risc_reset for more details.
        communicator_->all_tensix_reset_deassert(core.x, core.y);
    }
}

void RtlSimulationTTDevice::dma_d2h(void* dst, uint32_t src, size_t size) {
    UMD_THROW(error::RuntimeError, "dma_d2h() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) {
    UMD_THROW(error::RuntimeError, "dma_d2h_zero_copy() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::dma_h2d(uint32_t dst, const void* src, size_t size) {
    UMD_THROW(error::RuntimeError, "dma_h2d() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) {
    UMD_THROW(error::RuntimeError, "dma_h2d_zero_copy() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "read_from_arc_apb() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::write_to_arc_apb(
    const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "write_to_arc_apb() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "read_from_arc_csm() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::write_to_arc_csm(
    const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "write_to_arc_csm() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) {
    // RTL simulation doesn't have ARC cores in the same way.
}

std::chrono::milliseconds RtlSimulationTTDevice::wait_eth_core_training(
    const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms) {
    // RTL simulation doesn't require Ethernet training.
    return std::chrono::milliseconds(0);
}

EthTrainingStatus RtlSimulationTTDevice::read_eth_core_training_status(tt_xy_pair eth_core) {
    // RTL simulation doesn't require Ethernet training.
    return EthTrainingStatus::SUCCESS;
}

uint32_t RtlSimulationTTDevice::get_clock() {
    // RTL simulation does not have an ARC processor, so clock frequency is not available.
    UMD_THROW(error::RuntimeError, "get_clock() not supported for RTL simulation.");
}

uint32_t RtlSimulationTTDevice::get_min_clock_freq() {
    // RTL simulation does not have an ARC processor, so clock frequency is not available.
    UMD_THROW(error::RuntimeError, "get_min_clock_freq() not supported for RTL simulation.");
}

bool RtlSimulationTTDevice::get_noc_translation_enabled() {
    // NOC address translation is not available in RTL simulation.
    return false;
}

void RtlSimulationTTDevice::dma_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "dma_multicast_write() not supported for RTL simulation.");
}

void RtlSimulationTTDevice::retrain_dram_core(const uint32_t dram_channel) {
    UMD_THROW(error::RuntimeError, "DRAM retraining is not supported in RTL simulation device.");
}

void RtlSimulationTTDevice::noc_multicast_write(
    const void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) {
    multicast_write_via_unicast(src, size, core_start, core_end, addr);
}

void RtlSimulationTTDevice::noc_multicast_write(const void* src, size_t size, uint64_t addr, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "NOC multicast write is not supported in RTL simulation device.");
}

}  // namespace tt::umd
