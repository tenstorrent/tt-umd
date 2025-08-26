// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/firmware_versioner.h"

#include "umd/device/semver.hpp"
#include "umd/device/tt_device/blackhole_firmware_versioner.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_device/wormhole_firmware_versioner.h"

namespace tt::umd {

FirmwareVersioner::FirmwareVersioner(TTDevice* tt_device) : tt_device(tt_device) {}

std::unique_ptr<FirmwareVersioner> FirmwareVersioner::create_firmware_versioner(TTDevice* tt_device) {
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeFirmwareVersioner>(tt_device);
        case ARCH::BLACKHOLE:
            return std::make_unique<BlackholeFirmwareVersioner>(tt_device);
        default:
            throw std::runtime_error("Unsupported architecture for firmware versioner.");
    }
}

}  // namespace tt::umd
