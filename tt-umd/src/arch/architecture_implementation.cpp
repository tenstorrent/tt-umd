// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/architecture_implementation.hpp"

#include <memory>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<ArchitectureImplementation> ArchitectureImplementation::create(tt::ARCH architecture) {
    switch (architecture) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeImplementation>();
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeImplementation>();
        case tt::ARCH::QUASAR:
            return std::make_unique<GrendelImplementation>();
        default:
            UMD_THROW(error::RuntimeError, "No architecture implementation for " + arch_to_str(architecture) + ".");
    }
}

}  // namespace tt::umd
