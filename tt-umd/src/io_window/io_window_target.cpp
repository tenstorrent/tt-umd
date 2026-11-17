// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "io_window/io_window_target.hpp"

namespace tt::umd {

TargetIoWindowConfig make_io_window_target(
    const SocDescriptor& soc_descriptor,
    CoreCoord core_start,
    uint64_t addr,
    std::optional<NocId> noc,
    std::optional<CoreCoord> core_end,
    WindowFlags flags) {
    // The coordinates a NOC request carries depend on the NOC it travels on, so the NOC is fixed here and
    // handed to the window too; otherwise the window could route over a NOC the coordinates were not made for.
    const NocId noc_id = noc.value_or(get_selected_noc_id());
    std::optional<tt_xy_pair> translated_end;
    if (core_end.has_value()) {
        translated_end = soc_descriptor.translate_chip_coord_to_translated(*core_end, noc_id);
    }
    return TargetIoWindowConfig{
        soc_descriptor.translate_chip_coord_to_translated(core_start, noc_id), translated_end, addr, noc_id, flags};
}

}  // namespace tt::umd
