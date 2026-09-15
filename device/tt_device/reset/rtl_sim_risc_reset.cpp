// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/reset/rtl_sim_risc_reset.hpp"

#include <array>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/simulation/rtl_sim_communicator.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

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

RtlSimRiscReset::RtlSimRiscReset(RtlSimCommunicator* communicator, tt::ARCH arch, std::recursive_mutex& device_lock) :
    communicator_(communicator), arch_(arch), device_lock_(device_lock) {}

void RtlSimRiscReset::check_communicator() const {
    UMD_ASSERT(
        communicator_ != nullptr,
        error::RuntimeError,
        "RISC reset is not wired for a client-mode RTL simulation device.");
}

void RtlSimRiscReset::assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id) {
    check_communicator();
    std::lock_guard<std::recursive_mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}.", selected_riscs);
    // If the architecture is Quasar, a special case is needed to control the NEO Data Movement cores.
    if (arch_ == tt::ARCH::QUASAR) {
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

    if (arch_ != tt::ARCH::QUASAR || (selected_riscs & RiscType::ALL_NEO_TRISCS) != RiscType::NONE) {
        // In case of Wormhole and Blackhole, we don't check which cores are selected, we just assert all tensix cores.
        // So the functionality is if we called with RiscType::ALL_TENSIX or RiscType::ALL.
        // In case of Quasar, this won't assert the NEO Data Movement cores, but will assert the Tensix cores.
        // For simplicity, we don't check and try to list all the combinations of selected_riscs arguments, we just
        // always call this command as if reset for all was requested.
        communicator_->all_tensix_reset_assert(core.x, core.y);
    }
}

void RtlSimRiscReset::deassert_risc_reset(
    tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id) {
    check_communicator();
    std::lock_guard<std::recursive_mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    // See the comment in assert_risc_reset for more details.
    if (arch_ == tt::ARCH::QUASAR) {
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

    if (arch_ != tt::ARCH::QUASAR || (selected_riscs & RiscType::ALL_NEO_TRISCS) != RiscType::NONE) {
        // See the comment in assert_risc_reset for more details.
        communicator_->all_tensix_reset_deassert(core.x, core.y);
    }
}

std::optional<RiscType> RtlSimRiscReset::get_risc_reset_state(tt_xy_pair core, NocId noc_id) { return std::nullopt; }

}  // namespace tt::umd
