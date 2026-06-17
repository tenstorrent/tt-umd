// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>

#include "umd/device/utils/op_budget_timer.hpp"

using namespace tt::umd;
using namespace std::chrono_literals;

// An op that completes well within its budget is not over budget.
TEST(OpBudgetTimer, UnderBudgetReturnsFalse) {
    OpBudgetTimer timer(1h);
    EXPECT_FALSE(timer.record_and_check(std::chrono::steady_clock::now()));
}

// A zero budget makes any elapsed time an overrun, deterministically and without sleeping.
TEST(OpBudgetTimer, ZeroBudgetReturnsTrue) {
    OpBudgetTimer timer(0ms);
    EXPECT_TRUE(timer.record_and_check(std::chrono::steady_clock::now()));
}

// Passing a start timestamp in the past deterministically exceeds the budget without sleeping.
TEST(OpBudgetTimer, ElapsedPastBudgetReturnsTrue) {
    OpBudgetTimer timer(10ms);
    EXPECT_TRUE(timer.record_and_check(std::chrono::steady_clock::now() - 50ms));
}
