// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

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
     * @param use_noc1 Whether to use NOC1 for communication during construction.
     */
    WormholeArcMessenger(TTDevice* tt_device, bool use_noc1);

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     *
     * @param msg_code ARC messsage type.
     * @param return_values Return values from the ARC message.
     * @param args Arguments for the message. For Wormhole, only 2 args are allowed, each <= uint16_t max.
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     * @param use_noc1 Whether to use NOC1 for communication.
     */
    uint32_t send_message(
        const uint32_t msg_code,
        std::vector<uint32_t>& return_values,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT,
        bool use_noc1 = false) override;
};

}  // namespace tt::umd
