// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/io_window/io_window_target.hpp"

namespace tt::umd {

TargetIoWindowConfig make_io_window_target(
    const SocDescriptor& soc_descriptor,
    CoreCoord core_start,
    uint64_t addr,
    std::optional<NocId> noc,
    std::optional<CoreCoord> core_end,
    WindowFlags flags) {
    // Routing is resolved first, so the coordinates and the mapping they end up in agree on the NOC
    // even when the caller left the choice open.
    const NocId resolved_noc = noc.value_or(get_selected_noc_id());

    TargetIoWindowConfig target{
        .core_start = soc_descriptor.translate_chip_coord_to_translated(core_start, resolved_noc),
        .addr = addr,
        .noc = resolved_noc,
        .flags = flags,
    };
    if (core_end.has_value()) {
        target.core_end = soc_descriptor.translate_chip_coord_to_translated(core_end.value(), resolved_noc);
    }
    return target;
}

}  // namespace tt::umd
