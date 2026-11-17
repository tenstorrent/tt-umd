// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "io_window/io_window_target.hpp"

#include <cstdint>
#include <optional>

#include "common/unsupported.hpp"

namespace tt::umd {

TargetIoWindowConfig make_io_window_target(
    const SocDescriptor& soc_descriptor,
    CoreCoord core_start,
    uint64_t addr,
    std::optional<NocId> noc,
    std::optional<CoreCoord> core_end,
    WindowFlags flags) {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
