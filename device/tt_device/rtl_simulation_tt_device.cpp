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
#include "umd/device/types/core_coordinates.hpp"
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
    SimulationTTDevice(
        simulator_directory, std::make_unique<SimulationSysmemManager>(num_host_mem_channels, soc_descriptor.arch)),
    communicator_(std::make_unique<RtlSimCommunicator>(simulator_directory)) {
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
    SimulationTTDevice(std::move(client)) {
    set_soc_descriptor(soc_descriptor);
    arch = soc_descriptor.arch;
    architecture_impl_ = architecture_implementation::create(soc_descriptor.arch);

    // Client mode: the lifecycle drives the remote host over the socket. read/write are not wired
    // here -- the SimulationClient has no device I/O yet -- so those throw until the API grows.
    // create_client() has already validated that client_ is non-null.
    setup_ = [this] { attach_client(); };
    teardown_ = [this] { detach_client(); };
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

    init_tlb_allocator(/*bar0_base=*/0);
    setup_cached_tlb_window();
}

std::unique_ptr<TlbWindow> RtlSimulationTTDevice::create_tlb_window(
    int tlb_index, size_t size, TlbMapping mapping, tlb_data config) {
    auto handle = RtlSimTlbHandle::create(tlb_allocator_, tlb_index, size, mapping);
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

void RtlSimulationTTDevice::tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) {
    communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
}

void RtlSimulationTTDevice::tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) {
    communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
}

bool RtlSimulationTTDevice::handle_special_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    return smn_read(mem_ptr, core, addr, size);
}

bool RtlSimulationTTDevice::handle_special_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    return smn_write(mem_ptr, core, addr, size);
}

bool RtlSimulationTTDevice::smn_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    NocId noc_id = get_selected_noc_id();
    validate_noc_for_arch(noc_id, get_soc_descriptor().arch);
    if (noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
        return true;
    }
    return false;
}

bool RtlSimulationTTDevice::smn_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, addr, core.str());
    NocId noc_id = get_selected_noc_id();
    validate_noc_for_arch(noc_id, get_soc_descriptor().arch);
    if (noc_id == NocId::SYSTEM_NOC) {
        communicator_->smn_tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
        return true;
    }
    return false;
}

void RtlSimulationTTDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}.", selected_riscs);
    // If the architecture is Quasar, a special case is needed to control the NEO Data Movement cores.
    if (get_soc_descriptor().arch == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL) {
            communicator_->all_tensix_reset_assert(translated_core.x, translated_core.y);
            communicator_->all_neo_dms_reset_assert(translated_core.x, translated_core.y);
            communicator_->all_neo_dms_uncore_reset_assert();
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            communicator_->all_neo_dms_reset_assert(translated_core.x, translated_core.y);
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS_UNCORE) {
            communicator_->all_neo_dms_uncore_reset_assert();
            return;
        }
        if ((selected_riscs & RiscType::NEO_DM_UNCORE) != RiscType::NONE) {
            communicator_->neo_dm_uncore_reset_assert(translated_core.x, translated_core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_assert(translated_core.x, translated_core.y, i);
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
        communicator_->all_tensix_reset_assert(translated_core.x, translated_core.y);
    }
}

void RtlSimulationTTDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    // See the comment in assert_risc_reset for more details.
    if (get_soc_descriptor().arch == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL) {
            communicator_->all_neo_dms_uncore_reset_deassert();
            communicator_->all_neo_dms_reset_deassert(translated_core.x, translated_core.y);
            communicator_->all_tensix_reset_deassert(translated_core.x, translated_core.y);
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            communicator_->all_neo_dms_reset_deassert(translated_core.x, translated_core.y);
            return;
        }
        if (selected_riscs == RiscType::ALL_NEO_DMS_UNCORE) {
            communicator_->all_neo_dms_uncore_reset_deassert();
            return;
        }
        if ((selected_riscs & RiscType::NEO_DM_UNCORE) != RiscType::NONE) {
            communicator_->neo_dm_uncore_reset_deassert(translated_core.x, translated_core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_deassert(translated_core.x, translated_core.y, i);
            }
        }
    }

    if (get_soc_descriptor().arch != tt::ARCH::QUASAR ||
        (selected_riscs & RiscType::ALL_NEO_TRISCS) != RiscType::NONE) {
        // See the comment in assert_risc_reset for more details.
        communicator_->all_tensix_reset_deassert(translated_core.x, translated_core.y);
    }
}

void RtlSimulationTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) {
    // RTL simulation doesn't have ARC cores in the same way.
}

std::chrono::milliseconds RtlSimulationTTDevice::wait_eth_core_training(
    CoreCoord eth_core, const std::chrono::milliseconds timeout_ms) {
    // RTL simulation doesn't require Ethernet training.
    return std::chrono::milliseconds(0);
}

EthTrainingStatus RtlSimulationTTDevice::read_eth_core_training_status(CoreCoord eth_core) {
    // RTL simulation doesn't require Ethernet training.
    return EthTrainingStatus::SUCCESS;
}

}  // namespace tt::umd
