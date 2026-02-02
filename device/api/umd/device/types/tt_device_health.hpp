// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>

namespace tt::umd {

enum class TTDeviceInitResult {
    UNKNOWN = 0,
    UNINITIALIZED,
    NOC0_FAILED,
    NOC1_FAILED,
    ARC_STARTUP_FAILED,
    ARC_MESSENGER_UNAVAILABLE,
    ARC_TELEMETRY_UNAVAILABLE,
    FIRMWARE_INFO_PROVIDER_UNAVAILABLE,
    SUCCESSFUL,
};

}  // namespace tt::umd
