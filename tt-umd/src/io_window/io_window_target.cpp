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
    std::optional<tt_xy_pair> translated_end;
    if (core_end.has_value()) {
        translated_end = soc_descriptor.translate_coord_to(*core_end, CoordSystem::TRANSLATED);
    }
    return TargetIoWindowConfig{
        soc_descriptor.translate_coord_to(core_start, CoordSystem::TRANSLATED), translated_end, addr, noc, flags};
}

}  // namespace tt::umd
