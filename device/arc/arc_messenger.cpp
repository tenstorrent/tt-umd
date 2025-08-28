/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/arc/arc_messenger.h"

#include "umd/device/arc/blackhole_arc_messenger.h"
#include "umd/device/arc/wormhole_arc_messenger.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/umd_utils.h"

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

ArcMessenger::ArcMessenger(TTDevice* tt_device) : tt_device(tt_device) {
    lock_manager.initialize_mutex(
        MutexType::ARC_MSG, tt_device->get_communication_device_id(), tt_device->get_communication_device_type());
    lock_manager.initialize_mutex(
        MutexType::REMOTE_ARC_MSG,
        tt_device->get_communication_device_id(),
        tt_device->get_communication_device_type());
    // TODO: Remove this once we have proper mutex usage
    lock_manager.initialize_mutex(MutexType::ARC_MSG);
}

uint32_t ArcMessenger::send_message(const uint32_t msg_code, uint16_t arg0, uint16_t arg1, uint32_t timeout_ms) {
    std::vector<uint32_t> return_values;
    return send_message(msg_code, return_values, arg0, arg1, timeout_ms);
}

ArcMessenger::~ArcMessenger() {
    lock_manager.clear_mutex(
        MutexType::ARC_MSG, tt_device->get_communication_device_id(), tt_device->get_communication_device_type());
    lock_manager.clear_mutex(
        MutexType::REMOTE_ARC_MSG,
        tt_device->get_communication_device_id(),
        tt_device->get_communication_device_type());
}

}  // namespace tt::umd
