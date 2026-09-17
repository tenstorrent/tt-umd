// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Controls the reset state of the RISC cores on a device.
 */
class RiscReset {
public:
    virtual ~RiscReset() = default;

    /**
     * @brief Puts the selected RISC cores into reset.
     * @param core Target core, in translated coordinates resolved for noc_id.
     * @param selected_riscs Which RISCs to put into reset.
     * @param noc_id NOC to route through (if the feature is routed through NOC).
     */
    virtual void assert_risc_reset(
        tt_xy_pair core, RiscType selected_riscs, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Releases the selected RISC cores from reset.
     * @param core Target core, in translated coordinates resolved for noc_id.
     * @param selected_riscs Which RISCs to release from reset.
     * @param staggered_start If true, staggers the startup of the RISCs.
     * @param noc_id NOC to route through (if the feature is routed through NOC).
     */
    virtual void deassert_risc_reset(
        tt_xy_pair core,
        RiscType selected_riscs,
        bool staggered_start,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Reads which RISC cores are currently held in reset.
     * @param core Target core, in translated coordinates resolved for noc_id.
     * @param noc_id NOC to route through (if the feature is routed through NOC).
     * @return The RISCs in reset, or std::nullopt if the implementation cannot answer.
     */
    virtual std::optional<RiscType> get_risc_reset_state(
        tt_xy_pair core, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;
};

}  // namespace tt::umd
