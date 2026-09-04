// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arch/architecture_implementation.hpp"

#include <memory>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"

namespace tt::umd {

std::unique_ptr<ArchitectureImplementation> ArchitectureImplementation::create(tt::ARCH architecture) {
    switch (architecture) {
        case tt::ARCH::QUASAR:
        case tt::ARCH::GRENDEL:
            return std::make_unique<GrendelImplementation>();
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeImplementation>();
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeImplementation>();
        default:
            return nullptr;
    }
}

}  // namespace tt::umd
