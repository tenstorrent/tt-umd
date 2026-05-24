// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace tt::umd {

/**
 * @defgroup tt_firmware_messenger FirmwareMessenger
 * @{
 *
 * @brief Host-to-firmware command channel.
 *
 * Sends commands to the management firmware running on a device and returns
 * the result, regardless of the underlying firmware and hardware configuration.
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref DeviceCommandResult | Exit code and return values from a firmware command |
 *
 */

class FirmwareMessenger {
public:
    virtual ~FirmwareMessenger() = default;

    /**
     * @brief Sends a command to the device firmware and waits for the result.
     *
     * @param msg_code Command identifier understood by the firmware.
     * @param args Arguments for the command (device-specific limits apply).
     * @param timeout Timeout for the command to complete.
     * @return DeviceCommandResult The exit code and any return values from the firmware.
     */
    virtual DeviceCommandResult send_message(
        uint32_t msg_code,
        const std::vector<uint32_t>& args = {},
        std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) = 0;
};

/** @} */  // end of tt_firmware_messenger group

}  // namespace tt::umd
