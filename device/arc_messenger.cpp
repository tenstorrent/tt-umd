/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/arc_messenger.h"

#include "umd/device/blackhole_arc_messenger.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/wormhole_arc_messenger.h"

namespace tt::umd {

std::unique_ptr<ArcMessenger> ArcMessenger::create_arc_messenger(TTDevice* tt_device) {
    tt::ARCH arch = tt_device->get_arch();

    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeArcMessenger>(tt_device);
            break;
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeArcMessenger>(tt_device);
            break;
        default:
            throw std::runtime_error("Unsupported architecture for creating ArcMessenger.");
    }
}

ArcMessenger::ArcMessenger(TTDevice* tt_device) : tt_device(tt_device) {}

uint32_t ArcMessenger::send_message(const uint32_t msg_code, uint16_t arg0, uint16_t arg1, uint32_t timeout_ms) {
    std::vector<uint32_t> return_values;
    return send_message(msg_code, return_values, arg0, arg1, timeout_ms);
}

}  // namespace tt::umd
