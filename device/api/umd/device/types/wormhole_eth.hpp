// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include "umd/device/utils/semver.hpp"

namespace tt::umd::wormhole {

// ETH related constants.
// Tied to the wormhole ETH FW layout.
inline constexpr uint32_t ETH_TRAIN_STATUS_ADDR = 0x1104;
inline constexpr uint32_t ETH_RETRAIN_ADDR = 0x1EFC;
inline constexpr uint32_t ETH_LINK_ERR_STATUS_ADDR = 0x1440;
inline constexpr uint32_t ETH_TRIGGER_RETRAIN_VAL = 1;
inline constexpr uint32_t ETH_FW_VERSION_ADDR = 0x210;
// Minimum ETH FW version required to support triggering a retrain.
inline constexpr SemVer MIN_ETH_FW_VERSION_FOR_RETRAIN = SemVer(7, 2, 0);

// Not connected:     LINK_INACTIVE_TIMEOUT_SIGDET: 11
// Not connected:     LINK_INACTIVE_TIMEOUT_PG_RCV: 12
// Unused:        LINK_INACTIVE_PORT_NOT_POPULATED: 13
// Port disabled:    LINK_INACTIVE_PORT_MASKED_OFF: 14
// On GLX6U with ETH train issues, the error status was 2, everything up to 4 is interpreted as config error.
inline constexpr uint32_t ETH_LINK_UNUSED_ERROR_CODE_RANGE_START = 11;

inline constexpr uint32_t ETH_POSTCODE_ADDR = 0xFFB3010C;
inline constexpr uint32_t ETH_HEARTBEAT_ADDR = 0x1F80;  // test_results[48];

}  // namespace tt::umd::wormhole
