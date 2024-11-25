// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device.h"

#include "umd/device/tt_device_blackhole.h"
#include "umd/device/tt_device_grayskull.h"
#include "umd/device/tt_device_wormhole.h"

namespace tt::umd {

std::unique_ptr<TTDevice> TTDevice::create(tt::ARCH architecture) {
    switch (architecture) {
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeTTDevice>();
        case tt::ARCH::GRAYSKULL:
            return std::make_unique<GrayskullTTDevice>();
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeTTDevice>();
        default:
            return nullptr;
    }
}

}  // namespace tt::umd
