// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arc/blackhole_arc_message_queue.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class BlackholeArcMessenger : public ArcMessenger {
public:
    /**
     * Constructor for BlackholeArcMessenger.
     *
     * @param tt_device TTDevice object used to communicate with the ARC of the device.
     */
    BlackholeArcMessenger(TTDevice* tt_device);

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     *
     * @param msg_code ARC messsage type.
     * @param return_values Return values from the ARC message.
     * @param args Arguments for the message. For Blackhole, up to 7 args are allowed.
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     */
    uint32_t send_message(
        const uint32_t msg_code,
        std::vector<uint32_t>& return_values,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT) override;

private:
    std::unique_ptr<BlackholeArcMessageQueue> blackhole_arc_msg_queue = nullptr;
};

}  // namespace tt::umd
