// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device/tt_device/tt_device.h"

#include "device/tt_device/blackhole_tt_device.h"
#include "device/tt_device/grayskull_tt_device.h"
#include "device/tt_device/wormhole_tt_device.h"

namespace tt::umd {

std::unique_ptr<TTDevice> TTDevice::create(architecture architecture) {
    switch (architecture) {
        case architecture::blackhole: return std::make_unique<BlackholeTTDevice>();
        case architecture::grayskull: return std::make_unique<GrayskullTTDevice>();
        case architecture::wormhole:
        case architecture::wormhole_b0: return std::make_unique<WormholeTTDevice>();
        default: return nullptr;
    }
}

}  // namespace tt::umd
