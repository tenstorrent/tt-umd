// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace tt::umd {

/**
 * @brief Result of a firmware command execution.
 *
 * Bundles the exit code and any return values from the firmware into a single
 * return type, eliminating the need for out-parameters.
 */
struct DeviceCommandResult {
    uint32_t exit_code;
    std::vector<uint32_t> return_values;
};

/**
 * @brief Abstract interface for host-to-firmware communication.
 *
 * Represents a command channel to the management firmware running on a device,
 * regardless of the underlying firmware and hardware configuration.
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

}  // namespace tt::umd
