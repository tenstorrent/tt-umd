// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device/firmware/device_firmware.hpp"

namespace tt::umd {

/**
 * @brief Simulation implementation of DeviceFirmware.
 *
 * Simulators run no management firmware, so as the interface grows this implements each operation
 * as the appropriate no-op. Backends that need to differ get their own subclass when that need
 * arrives, not before.
 */
class SimulationDeviceFirmware : public DeviceFirmware {
public:
    // A simulated device's firmware is ready by construction, so there is nothing to wait for.
    void init_firmware(
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    // There is no management firmware to command; report success with no return values so shared
    // flows that issue commands unconditionally keep working against a simulated device.
    DeviceCommandResult send_device_command(
        [[maybe_unused]] uint32_t msg_code,
        [[maybe_unused]] const std::vector<uint32_t>& args,
        [[maybe_unused]] std::chrono::milliseconds timeout,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return DeviceCommandResult{};
    }
};

}  // namespace tt::umd
