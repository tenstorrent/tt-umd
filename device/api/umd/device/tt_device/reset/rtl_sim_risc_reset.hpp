// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>

#include "umd/device/tt_device/reset/risc_reset.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {
class RtlSimCommunicator;

/**
 * @brief RiscReset for an RTL simulation device: the backend takes reset as its own commands rather
 * than as a register write, with separate commands for Quasar's NEO, DM and uncore groups.
 *
 * The communicator is owned by the backend and must outlive this object. A device that only talks
 * to a remote host has none, and every operation then reports itself unwired.
 */
class RtlSimRiscReset : public RiscReset {
public:
    RtlSimRiscReset(RtlSimCommunicator* communicator, tt::ARCH arch, std::recursive_mutex& device_lock);

    void assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id = NocId::DEFAULT_NOC) override;

    void deassert_risc_reset(
        tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id = NocId::DEFAULT_NOC) override;

    std::optional<RiscType> get_risc_reset_state(tt_xy_pair core, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    RtlSimCommunicator* communicator_ = nullptr;
    tt::ARCH arch_ = tt::ARCH::Invalid;
    std::recursive_mutex& device_lock_;
};

}  // namespace tt::umd
