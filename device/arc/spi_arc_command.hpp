// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <vector>

#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

// The SPI flows were written against ArcMessenger::send_message, which fills the caller's
// return-value vector in place and returns the exit code. Kept as an adapter shared by the SPI
// devices so the conversion to DeviceFirmware does not also rewrite the flows themselves. A named
// header rather than a per-file anonymous namespace because unity builds (tt-metal's) merge those
// into one translation unit, where two copies collide.
inline uint32_t send_spi_arc_command(
    DeviceFirmware* firmware,
    uint32_t msg_code,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args = {}) {
    // The ArcMessenger path this replaces read the thread-selected NOC itself; keep that.
    DeviceCommandResult result =
        firmware->send_device_command(msg_code, args, timeout::ARC_MESSAGE_TIMEOUT, get_selected_noc_id());
    for (size_t i = 0; i < return_values.size() && i < result.return_values.size(); i++) {
        return_values[i] = result.return_values[i];
    }
    return result.exit_code;
}

}  // namespace tt::umd
