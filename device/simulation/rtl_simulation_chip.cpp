// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_simulation_chip.hpp"

#include <iostream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<RtlSimulationChip>(), "RtlSimulationChip must be non-abstract.");

// Vector of DM RiscType values for iteration.
static const std::vector<RiscType> RISC_TYPES_DMS = {
    RiscType::DM0,
    RiscType::DM1,
    RiscType::DM2,
    RiscType::DM3,
    RiscType::DM4,
    RiscType::DM5,
    RiscType::DM6,
    RiscType::DM7};

RtlSimulationChip::RtlSimulationChip(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels),
    communicator_(std::make_unique<RtlSimCommunicator>(simulator_directory)) {
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation device");
    communicator_->initialize();
}

void RtlSimulationChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    // Communicator is already initialized in constructor.
}

void RtlSimulationChip::close_device() { communicator_->shutdown(); }

void RtlSimulationChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, l1_dest, core.str());
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    communicator_->tile_write_bytes(translate_core.x, translate_core.y, l1_dest, src, size);
}

void RtlSimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    communicator_->tile_read_bytes(translate_core.x, translate_core.y, l1_src, dest, size);
}

void RtlSimulationChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
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

void RtlSimulationChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset({0, 0}, soft_resets);
}

void RtlSimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    // If the architecture is Quasar, a special case is needed to control the NEO Data Movement cores.
    if (arch_name == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            // Reset all DM cores.
            communicator_->all_neo_dms_reset_assert(translate_core.x, translate_core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_assert(translate_core.x, translate_core.y, i);
            }
        }
    }

    if (arch_name != tt::ARCH::QUASAR || (selected_riscs | RiscType::ALL_NEO_DMS) == RiscType::NONE) {
        // In case of Wormhole and Blackhole, we don't check which cores are selected, we just assert all tensix cores.
        // So the functionality is if we called with RiscType::ALL_TENSIX or RiscType::ALL.
        // In case of Quasar, this won't assert the NEO Data Movement cores, but will assert the Tensix cores.
        // For simplicity, we don't check and try to list all the combinations of selected_riscs arguments, we just
        // always call this command as if reset for all was requested.
        communicator_->all_tensix_reset_assert(translate_core.x, translate_core.y);
    }
}

void RtlSimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    // See the comment in assert_risc_reset for more details.
    if (arch_name == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            // Reset all DM cores.
            communicator_->all_neo_dms_reset_deassert(translate_core.x, translate_core.y);
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                communicator_->neo_dm_reset_deassert(translate_core.x, translate_core.y, i);
            }
        }
    }

    if (arch_name != tt::ARCH::QUASAR || (selected_riscs | RiscType::ALL_NEO_DMS) == RiscType::NONE) {
        // See the comment in assert_risc_reset for more details.
        communicator_->all_tensix_reset_deassert(translate_core.x, translate_core.y);
    }
}

}  // namespace tt::umd
