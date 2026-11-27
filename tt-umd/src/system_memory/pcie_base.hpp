// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "tt-umd/types/arch.hpp"

namespace tt::umd {

// Start of the NOC address window through which a device of this architecture reaches host memory, or 0 for an
// architecture without one.
inline uint64_t get_pcie_base_for_arch(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return 0x800000000;
        case tt::ARCH::BLACKHOLE:
            return 4ULL << 58;
        default:
            return 0;
    }
}

}  // namespace tt::umd
