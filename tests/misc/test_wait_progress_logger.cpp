// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>

#include "wait_progress_logger.hpp"

using namespace tt::umd::utils;
using namespace std::chrono_literals;

// A tick right after construction never logs (also covers the "very first tick" case); a wait
// that finishes before the first log_interval elapses stays silent.
TEST(WaitProgressLogger, TickBeforeIntervalDoesNotLog) {
    WaitProgressLogger logger("something", 1min, 5s);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(logger.tick(start));
    EXPECT_FALSE(logger.tick(start + 1s));
}

// Once log_interval has passed since the first tick, the next tick logs. The boundary itself
// (now - last_logged_ == log_interval_) counts as due, since the check is a strict `<`.
TEST(WaitProgressLogger, TickAfterIntervalLogs) {
    WaitProgressLogger logger("something", 1min, 5s);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(logger.tick(start));
    EXPECT_TRUE(logger.tick(start + 5s));
}

// After logging, the interval resets: the next log only fires log_interval after the previous
// one, not log_interval after the very first tick.
TEST(WaitProgressLogger, IntervalResetsAfterEachLog) {
    WaitProgressLogger logger("something", 1min, 5s);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(logger.tick(start));
    EXPECT_TRUE(logger.tick(start + 6s));
    EXPECT_FALSE(logger.tick(start + 10s));
    EXPECT_TRUE(logger.tick(start + 12s));
}
