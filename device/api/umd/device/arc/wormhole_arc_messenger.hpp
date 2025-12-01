/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class WormholeArcMessenger : public ArcMessenger {
public:
    /**
     * Constructor for WormholeArcMessenger.
     *
     * @param tt_device TTDevice object used to communicate with the ARC of the device.
     */
    WormholeArcMessenger(TTDevice* tt_device);

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     *
     * @param msg_code ARC messsage type.
     * @param return_values Return values from the ARC message.
     * @param args Arguments for the message. For Wormhole, only 2 args are allowed, each <= uint16_t max.
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     */
    uint32_t send_message(
        const uint32_t msg_code,
        std::vector<uint32_t>& return_values,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT) override;
};

}  // namespace tt::umd
