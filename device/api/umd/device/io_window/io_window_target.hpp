// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/io_window_config.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

/**
 * @brief Builds an IoWindow target from coordinates in any coordinate system.
 *
 * A TargetIoWindowConfig names its cores in the translated system, which a caller holding logical or
 * NOC coordinates cannot produce on its own. This resolves them against the given chip's mapping, so
 * a caller that has a SocDescriptor can reach the same factory that takes a target directly.
 *
 * @param soc_descriptor Coordinate mapping of the chip the window will target.
 * @param core_start Target core, or upper-left corner of a multicast grid.
 * @param addr Destination address on the target core(s).
 * @param noc Routing selection, or nullopt to route over the NOC selected for this thread.
 * @param core_end Lower-right corner of a multicast grid, or nullopt for unicast.
 * @param flags Transaction attributes.
 */
TargetIoWindowConfig make_io_window_target(
    const SocDescriptor& soc_descriptor,
    CoreCoord core_start,
    uint64_t addr,
    std::optional<NocId> noc = std::nullopt,
    std::optional<CoreCoord> core_end = std::nullopt,
    WindowFlags flags = WindowFlags::None);

}  // namespace tt::umd
