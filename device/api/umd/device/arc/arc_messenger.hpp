// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class TTDevice;

class ArcMessenger {
public:
    /**
     * Create an ArcMessenger object.
     *
     * @param tt_device TTDevice object used to communicate with the ARC of the device.
     * @param use_noc1 Whether to use NOC1 for communication during construction.
     * @return Unique pointer to ArcMessenger object.
     */
    static std::unique_ptr<ArcMessenger> create_arc_messenger(TTDevice* tt_device, bool use_noc1 = false);

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     *
     * @param msg_code ARC messsage type.
     * @param return_values Return values from the ARC message.
     * @param args Arguments for the message (device-specific limits apply).
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     * @param use_noc1 Whether to use NOC1 for communication.
     * @return Success code of the ARC message.
     */
    virtual uint32_t send_message(
        const uint32_t msg_code,
        std::vector<uint32_t>& return_values,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT,
        bool use_noc1 = false) = 0;

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     * This version of the function can be called if the return values are not needed.
     *
     * @param msg_code ARC messsage type.
     * @param args Arguments for the message (device-specific limits apply).
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     * @param use_noc1 Whether to use NOC1 for communication.
     * @return Success code of the ARC message.
     */
    uint32_t send_message(
        const uint32_t msg_code,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT,
        bool use_noc1 = false);

    virtual ~ArcMessenger();

protected:
    /**
     * Constructor for ArcMessenger.
     *
     * @param tt_device TTDevice object used to communicate with the ARC of the device.
     */
    ArcMessenger(TTDevice* tt_device);

    TTDevice* tt_device;
    LockManager lock_manager;
};

}  // namespace tt::umd
