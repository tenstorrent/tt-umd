// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"

#include <array>
#include <filesystem>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/simulation/simulation_chip.hpp"

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

std::unique_ptr<RtlSimulationTTDevice> RtlSimulationTTDevice::create(const std::filesystem::path& simulator_directory) {
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    SocDescriptor soc_descriptor = SocDescriptor(soc_desc_path);
    return std::make_unique<RtlSimulationTTDevice>(simulator_directory, soc_descriptor, DEFAULT_CHIP_ID);
}

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id) :
    communicator_(std::make_unique<RtlSimCommunicator>(simulator_directory)),
    simulator_directory_(simulator_directory),
    soc_descriptor_(std::move(soc_descriptor)),
    architecture_impl_(architecture_implementation::create(soc_descriptor_.arch)) {
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation TTDevice");
    arch = soc_descriptor_.arch;
    communicator_->initialize();
}

RtlSimulationTTDevice::~RtlSimulationTTDevice() { close_device(); }

void RtlSimulationTTDevice::start_device() {
    // Communicator is already initialized in constructor.
}

void RtlSimulationTTDevice::close_device() { communicator_->shutdown(); }

void RtlSimulationTTDevice::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, addr, core.str());
    communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
}

void RtlSimulationTTDevice::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
}

// Three overloads exist for send_tensix_risc_reset:
// 1. (tt_xy_pair, TensixSoftResetOptions) - main implementation used by C++ callers.
// 2. (tt_xy_pair, bool) - convenience wrapper used by the Python (nanobind) bindings.
// 3. (TensixSoftResetOptions) - required by the chip-level interface contract; throws because RTL
//    simulation always requires an explicit core coordinate.
void RtlSimulationTTDevice::send_tensix_risc_reset(
    tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (soft_resets == TENSIX_ASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending assert_risc_reset signal..");
        communicator_->all_tensix_reset_assert(translated_core.x, translated_core.y);
    } else if (soft_resets == TENSIX_DEASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
        communicator_->all_tensix_reset_deassert(translated_core.x, translated_core.y);
    } else {
        TT_THROW("Invalid soft reset option.");
    }
}

void RtlSimulationTTDevice::send_tensix_risc_reset(tt_xy_pair translated_core, bool deassert) {
    send_tensix_risc_reset(translated_core, deassert ? TENSIX_DEASSERT_SOFT_RESET : TENSIX_ASSERT_SOFT_RESET);
}

void RtlSimulationTTDevice::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    TT_THROW("send_tensix_risc_reset without core not supported for RTL simulation");
}

void RtlSimulationTTDevice::assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    // If the architecture is Quasar, a special case is needed to control the NEO Data Movement cores.
    if (soc_descriptor_.arch == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            // Reset all DM cores.
            communicator_->all_neo_dms_reset_assert(core.x, core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_assert(core.x, core.y, i);
            }
        }
    }

    if (soc_descriptor_.arch != tt::ARCH::QUASAR || (selected_riscs | RiscType::ALL_NEO_DMS) == RiscType::NONE) {
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
    if (soc_descriptor_.arch == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            // Reset all DM cores.
            communicator_->all_neo_dms_reset_deassert(core.x, core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_deassert(core.x, core.y, i);
            }
        }
    }

    if (soc_descriptor_.arch != tt::ARCH::QUASAR || (selected_riscs | RiscType::ALL_NEO_DMS) == RiscType::NONE) {
        // See the comment in assert_risc_reset for more details.
        communicator_->all_tensix_reset_deassert(core.x, core.y);
    }
}

void RtlSimulationTTDevice::dma_d2h(void* dst, uint32_t src, size_t size) {
    TT_THROW("dma_d2h not supported for RTL simulation");
}

void RtlSimulationTTDevice::dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) {
    TT_THROW("dma_d2h_zero_copy not supported for RTL simulation");
}

void RtlSimulationTTDevice::dma_h2d(uint32_t dst, const void* src, size_t size) {
    TT_THROW("dma_h2d not supported for RTL simulation");
}

void RtlSimulationTTDevice::dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) {
    TT_THROW("dma_h2d_zero_copy not supported for RTL simulation");
}

void RtlSimulationTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    TT_THROW("read_from_arc_apb not supported for RTL simulation");
}

void RtlSimulationTTDevice::write_to_arc_apb(
    const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    TT_THROW("write_to_arc_apb not supported for RTL simulation");
}

void RtlSimulationTTDevice::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    TT_THROW("read_from_arc_csm not supported for RTL simulation");
}

void RtlSimulationTTDevice::write_to_arc_csm(
    const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    TT_THROW("write_to_arc_csm not supported for RTL simulation");
}

bool RtlSimulationTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) {
    // RTL simulation doesn't have ARC cores in the same way.
    return true;
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
    TT_THROW("get_clock not supported for RTL simulation");
}

uint32_t RtlSimulationTTDevice::get_min_clock_freq() {
    // RTL simulation does not have an ARC processor, so clock frequency is not available.
    TT_THROW("get_min_clock_freq not supported for RTL simulation");
}

bool RtlSimulationTTDevice::get_noc_translation_enabled() {
    // NOC address translation is not available in RTL simulation.
    return false;
}

void RtlSimulationTTDevice::dma_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    TT_THROW("dma_multicast_write not supported for RTL simulation");
}

void RtlSimulationTTDevice::retrain_dram_core(const uint32_t dram_channel) {
    throw std::runtime_error("DRAM retraining is not supported in RTL simulation device.");
}

}  // namespace tt::umd
