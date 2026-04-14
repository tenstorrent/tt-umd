// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_umd/arch/architecture_implementation.hpp"

#include <memory>

#include "tt_umd/arch/blackhole_implementation.hpp"
#include "tt_umd/arch/grendel_implementation.hpp"
#include "tt_umd/arch/wormhole_implementation.hpp"

namespace tt::umd {

std::unique_ptr<architecture_implementation> architecture_implementation::create(tt::ARCH architecture) {
    switch (architecture) {
        case tt::ARCH::QUASAR:
            return std::make_unique<grendel_implementation>();
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<blackhole_implementation>();
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<wormhole_implementation>();
        default:
            return nullptr;
    }
}

}  // namespace tt::umd
