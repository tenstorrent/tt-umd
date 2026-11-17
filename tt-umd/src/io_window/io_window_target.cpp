// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "io_window/io_window_target.hpp"

#include <cstdint>
#include <optional>

namespace tt::umd {

TargetIoWindowConfig make_io_window_target(
    const SocDescriptor& soc_descriptor,
    CoreCoord core_start,
    uint64_t addr,
    std::optional<NocId> noc,
    std::optional<CoreCoord> core_end,
    WindowFlags flags) {
    // Which coordinates reach a core depends on the NOC, so the NOC is fixed here, where the coordinates are
    // resolved, rather than left for the window to pick when it is configured.
    const NocId noc_id = noc.value_or(get_selected_noc_id());
    TargetIoWindowConfig target{};
    target.core_start = soc_descriptor.translate_chip_coord_to_translated(core_start, noc_id);
    if (core_end.has_value()) {
        target.core_end = soc_descriptor.translate_chip_coord_to_translated(*core_end, noc_id);
    }
    target.addr = addr;
    target.noc = noc_id;
    target.flags = flags;
    return target;
}

}  // namespace tt::umd
