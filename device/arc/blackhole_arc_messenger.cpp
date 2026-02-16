// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_messenger.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

BlackholeArcMessenger::BlackholeArcMessenger(TTDevice* tt_device) : ArcMessenger(tt_device) {
    blackhole_arc_msg_queue = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        tt_device, BlackholeArcMessageQueueIndex::APPLICATION);
}

uint32_t BlackholeArcMessenger::send_message(
    const uint32_t msg_code,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms) {
    auto lock = lock_manager.acquire_mutex(MutexType::ARC_MSG, tt_device->get_pci_device()->get_device_num());
    return blackhole_arc_msg_queue->send_message((ArcMessageType)msg_code, args, timeout_ms);
}

}  // namespace tt::umd
