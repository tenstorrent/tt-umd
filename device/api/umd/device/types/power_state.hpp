// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

/**
 * @brief Requested hardware power domain state for a device.
 */
enum class PowerState : uint8_t {
    HIGH,  ///< Claims all power domains.
    LOW,   ///< Releases power domains, allowing the device to enter lower power states.
};

}  // namespace tt::umd
