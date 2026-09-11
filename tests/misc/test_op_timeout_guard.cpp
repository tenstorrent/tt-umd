// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <stdexcept>

#include "umd/device/utils/op_timeout_guard.hpp"

using namespace tt::umd;
using namespace std::chrono_literals;

// Within budget: the overrun action never runs.
TEST(OpTimeoutGuard, UnderBudgetDoesNotFire) {
    bool fired = false;
    auto guard = OpTimeoutGuard(1h, std::function<bool()>{}, [&](std::chrono::nanoseconds, int) { fired = true; });
    guard.record_and_check(std::chrono::steady_clock::now(), 42);
    EXPECT_FALSE(fired);
}

// A budget of 0 disables the check entirely, so even a long-elapsed op never fires the action.
TEST(OpTimeoutGuard, ZeroBudgetDisablesCheck) {
    bool fired = false;
    auto guard = OpTimeoutGuard(0ms, std::function<bool()>{}, [&](std::chrono::nanoseconds, int) { fired = true; });
    guard.record_and_check(std::chrono::steady_clock::now() - 1h, 42);
    EXPECT_FALSE(fired);
}

// Over budget with no veto callback: the action fires. A start in the past exceeds the budget
// deterministically without sleeping.
TEST(OpTimeoutGuard, OverBudgetNoVetoFires) {
    bool fired = false;
    auto guard = OpTimeoutGuard(10ms, std::function<bool()>{}, [&](std::chrono::nanoseconds, int) { fired = true; });
    guard.record_and_check(std::chrono::steady_clock::now() - 50ms, 42);
    EXPECT_TRUE(fired);
}

// Over budget but the veto reports a false alarm (op healthy): the action is suppressed.
TEST(OpTimeoutGuard, FalseAlarmVetoSuppressesAction) {
    bool fired = false;
    auto guard = OpTimeoutGuard(
        10ms, [] { return true; }, [&](std::chrono::nanoseconds, int) { fired = true; });
    guard.record_and_check(std::chrono::steady_clock::now() - 50ms, 42);
    EXPECT_FALSE(fired);
}

// Over budget and the veto confirms it is not a false alarm: the action fires.
TEST(OpTimeoutGuard, ConfirmedOverrunFires) {
    bool fired = false;
    auto guard = OpTimeoutGuard(
        10ms, [] { return false; }, [&](std::chrono::nanoseconds, int) { fired = true; });
    guard.record_and_check(std::chrono::steady_clock::now() - 50ms, 42);
    EXPECT_TRUE(fired);
}

// The action may throw any exception; the guard propagates it.
TEST(OpTimeoutGuard, ActionThrowsArbitraryException) {
    auto guard = OpTimeoutGuard(
        10ms, std::function<bool()>{}, [](std::chrono::nanoseconds, int) { throw std::runtime_error("over budget"); });
    EXPECT_THROW(guard.record_and_check(std::chrono::steady_clock::now() - 50ms, 42), std::runtime_error);
}

// The measured delta and the caller's forwarded args reach the action.
TEST(OpTimeoutGuard, ForwardsDeltaAndArgs) {
    std::chrono::nanoseconds seen_delta{0};
    int seen_arg = 0;
    auto guard = OpTimeoutGuard(10ms, std::function<bool()>{}, [&](std::chrono::nanoseconds d, int a) {
        seen_delta = d;
        seen_arg = a;
    });
    guard.record_and_check(std::chrono::steady_clock::now() - 50ms, 7);
    EXPECT_GE(seen_delta, 50ms);
    EXPECT_EQ(seen_arg, 7);
}
