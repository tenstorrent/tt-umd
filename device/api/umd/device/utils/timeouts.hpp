/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>

namespace tt::umd::timeout {
inline constexpr auto NON_MMIO_RW_TIMEOUT = std::chrono::milliseconds(5'000);

inline constexpr auto ARC_MESSAGE_TIMEOUT = std::chrono::milliseconds(1'000);
inline constexpr auto ARC_STARTUP_TIMEOUT = std::chrono::milliseconds(1'000);
inline constexpr auto ARC_POST_RESET_TIMEOUT = std::chrono::milliseconds(1'000);
inline constexpr auto ARC_LONG_POST_RESET_TIMEOUT = std::chrono::milliseconds(300'000);

inline constexpr auto DRAM_TRAINING_TIMEOUT = std::chrono::milliseconds(60'000);
inline constexpr auto ETH_QUEUE_ENABLE_TIMEOUT = std::chrono::milliseconds(30'000);
inline constexpr auto ETH_TRAINING_TIMEOUT = std::chrono::milliseconds(60'000);

inline constexpr auto AICLK_TIMEOUT = std::chrono::milliseconds(100);

inline constexpr auto WARM_RESET_DEVICES_REAPPEAR_TIMEOUT = std::chrono::milliseconds(10'000);

inline constexpr auto UBB_WARM_RESET_TIMEOUT = std::chrono::milliseconds(100'000);
inline constexpr auto BH_WARM_RESET_TIMEOUT = std::chrono::milliseconds(2'000);
}  // namespace tt::umd::timeout
