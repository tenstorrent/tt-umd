// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd::wormhole {

// Tied to the wormhole ETH FW layout.
inline constexpr uint32_t ETH_TRAIN_STATUS_ADDR = 0x1104;
inline constexpr uint32_t ETH_RETRAIN_ADDR = 0x1EFC;
inline constexpr uint32_t ETH_LINK_ERR_STATUS_ADDR = 0x1440;
inline constexpr uint32_t ETH_TRIGGER_RETRAIN_VAL = 1;
inline constexpr uint32_t ETH_POSTCODE_ADDR = 0xFFB3010C;
inline constexpr uint32_t ETH_HEARTBEAT_ADDR = 0x1F80;  // test_results[48];

// Errors >= this code are used for reporting unconnected/unused ETH links (e.g. LINK_INACTIVE_TIMEOUT_SIGDET: 11),
// not true training failures.
inline constexpr uint32_t ETH_LINK_UNUSED_ERROR_CODE_RANGE_START = 11;

}  // namespace tt::umd::wormhole
