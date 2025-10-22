/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>

namespace tt::umd::timeout {
inline constexpr auto NON_MMIO_RW_TIMEOUT = std::chrono::milliseconds(5000);

inline constexpr auto ARC_MESSAGE_TIMEOUT = std::chrono::milliseconds(1000);
inline constexpr auto ARC_STARTUP_TIMEOUT = std::chrono::milliseconds(1000);
inline constexpr auto ARC_POST_RESET_TIMEOUT = std::chrono::milliseconds(1000);

inline constexpr auto DRAM_TRAINING_TIMEOUT = std::chrono::seconds(60);
inline constexpr auto ETH_TRAINING_TIMEOUT = std::chrono::seconds(60);
inline constexpr auto AICLK_TIMEOUT = std::chrono::milliseconds(100);

inline constexpr auto UBB_WARM_RESET_TIMEOUT = std::chrono::seconds(100);
inline constexpr auto BH_WARM_RESET_TIMEOUT = std::chrono::seconds(2);
}  // namespace tt::umd::timeout
