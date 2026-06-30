// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_messenger.hpp"

#include <fmt/ranges.h>

#include <chrono>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/utils/lock_manager.hpp"

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
    auto lock = lock_manager.acquire_mutex(
        MutexType::ARC_MSG, tt_device->get_communication_device_id(), tt_device->get_communication_device_type());
    uint32_t exit_code =
        blackhole_arc_msg_queue->send_message((ArcMessageType)msg_code, return_values, args, timeout_ms);
    log_debug(
        LogUMD,
        "ARC message 0x{:x} returned exit_code={} return_values=[{}]",
        msg_code,
        exit_code,
        fmt::join(return_values, ", "));
    return exit_code;
}

}  // namespace tt::umd
