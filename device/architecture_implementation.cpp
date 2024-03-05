// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device/architecture_implementation.h"

#include "device/blackhole_implementation.h"
#include "device/grayskull_implementation.h"
#include "device/wormhole_implementation.h"

namespace tt::umd {

std::unique_ptr<architecture_implementation> architecture_implementation::create(architecture architecture) {
    switch (architecture) {
        case architecture::blackhole: return std::make_unique<blackhole_implementation>();
        case architecture::grayskull: return std::make_unique<grayskull_implementation>();
        case architecture::wormhole:
        case architecture::wormhole_b0: return std::make_unique<wormhole_implementation>();
        default: return nullptr;
    }
}

}  // namespace tt::umd
