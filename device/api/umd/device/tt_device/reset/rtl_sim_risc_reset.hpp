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
 * @brief RiscReset over the RTL simulation backend: reset is driven through the communicator's
 * dedicated reset commands rather than a register write, with Quasar's NEO/DM/uncore groups
 * handled per selection.
 *
 * The injected dependencies are owned by the backend device and are not used during destruction.
 * A client-mode device has no communicator, so it installs this with nullptr and every operation
 * throws.
 */
class RtlSimRiscReset : public RiscReset {
public:
    RtlSimRiscReset(RtlSimCommunicator* communicator, tt::ARCH arch, std::recursive_mutex& device_lock);

    void assert_risc_reset(tt_xy_pair core, RiscType selected_riscs, NocId noc_id = NocId::DEFAULT_NOC) override;

    void deassert_risc_reset(
        tt_xy_pair core, RiscType selected_riscs, bool staggered_start, NocId noc_id = NocId::DEFAULT_NOC) override;

    std::optional<RiscType> get_risc_reset_state(tt_xy_pair core, NocId noc_id = NocId::DEFAULT_NOC) override;

private:
    void check_communicator() const;

    RtlSimCommunicator* communicator_ = nullptr;
    tt::ARCH arch_ = tt::ARCH::Invalid;
    std::recursive_mutex& device_lock_;
};

}  // namespace tt::umd
