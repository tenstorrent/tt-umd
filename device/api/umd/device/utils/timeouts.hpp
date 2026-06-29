// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

namespace tt::umd::timeout {
inline constexpr auto NON_MMIO_RW_TIMEOUT = std::chrono::milliseconds(5'000);

// Default per-op budget for a single host-side MMIO (TLB-mapped) transfer, overridable at runtime via
// MmioTimeoutConfig::set_op_timeout. A healthy MMIO op is microseconds. On the production path a longer
// transient stall is absorbed by the hang-detector veto (a healthy NOC lets the op continue), so the
// budget is kept tight to catch a genuinely hung NOC quickly. Paths that drive a TLB window without a
// veto wired (e.g. raw SiliconTlbWindow in tests) should widen it via MmioTimeoutConfig.
inline constexpr auto MMIO_OP_TIMEOUT = std::chrono::milliseconds(2);

inline constexpr auto ARC_MESSAGE_TIMEOUT = std::chrono::milliseconds(1'000);
// ARC clears the interrupt trigger bit quickly; this just guards against concurrent
// processes/threads opening clusters, which causes KMD to send ARC messages that
// may leave the trigger bit set momentarily.
inline constexpr auto ARC_TRIGGER_CLEAR_TIMEOUT = std::chrono::milliseconds(5);
inline constexpr auto ARC_STARTUP_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto ARC_POST_RESET_TIMEOUT = std::chrono::milliseconds(1'000);
inline constexpr auto ARC_LONG_POST_RESET_TIMEOUT = std::chrono::milliseconds(300'000);

inline constexpr auto DRAM_TRAINING_TIMEOUT = std::chrono::milliseconds(300'000);
inline constexpr auto ETH_QUEUE_ENABLE_TIMEOUT = std::chrono::milliseconds(30'000);
inline constexpr auto ETH_TRAINING_TIMEOUT = std::chrono::milliseconds(900'000);
inline constexpr auto ETH_STARTUP_TIMEOUT = std::chrono::milliseconds(10'000);
// Note: This was formerly 5ms, and it made some of our tests fail even when ETH was running.
// If adjusting, stress test using our whole test suite to see if the timeout is sufficient.
inline constexpr auto ETH_HEARTBEAT_TIMEOUT = std::chrono::milliseconds(50);

inline constexpr auto AICLK_TIMEOUT = std::chrono::milliseconds(200);

inline constexpr auto WARM_RESET_M3_TIMEOUT = std::chrono::milliseconds(20'000);
inline constexpr auto WARM_RESET_REAPPEAR_POLL_INTERVAL = std::chrono::milliseconds(100);
inline constexpr auto WARM_RESET_DEVICES_REAPPEAR_TIMEOUT = std::chrono::milliseconds(10'000);

inline constexpr auto UBB_WARM_RESET_TIMEOUT = std::chrono::milliseconds(100'000);
inline constexpr auto BH_WARM_RESET_TIMEOUT = std::chrono::milliseconds(2'000);
}  // namespace tt::umd::timeout
