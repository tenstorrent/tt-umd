// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>

#include "utils.hpp"

using namespace tt::umd::utils;
using namespace std::chrono_literals;

namespace {

// Short enough to keep the suite fast, well under WaitProgressLogger's log interval, so these tests
// cover poll_until's loop contract but not the log output itself.
constexpr auto TIMEOUT = std::chrono::milliseconds(20);
constexpr auto BUSY_POLL_WINDOW = std::chrono::microseconds(0);
constexpr auto POLL_INTERVAL = std::chrono::microseconds(100);

}  // namespace

// A predicate that is already true returns immediately, without a second check.
TEST(PollUntil, TrueOnFirstCallChecksOnce) {
    int calls = 0;
    EXPECT_TRUE(poll_until(
        [&] {
            ++calls;
            return true;
        },
        TIMEOUT,
        BUSY_POLL_WINDOW,
        POLL_INTERVAL));
    EXPECT_EQ(calls, 1);
}

// Polling stops on the first true, not after it.
TEST(PollUntil, TrueOnNthCallStopsThere) {
    int calls = 0;
    EXPECT_TRUE(poll_until([&] { return ++calls == 5; }, 1s, BUSY_POLL_WINDOW, POLL_INTERVAL));
    EXPECT_EQ(calls, 5);
}

TEST(PollUntil, NeverTrueTimesOut) {
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(poll_until([] { return false; }, TIMEOUT, BUSY_POLL_WINDOW, POLL_INTERVAL));
    EXPECT_GE(std::chrono::steady_clock::now() - start, TIMEOUT);
}

// An exhausted budget (e.g. wait_eth_cores_training's shared budget after an earlier core used it
// up) still gets exactly one check before timing out.
TEST(PollUntil, NegativeTimeoutChecksOnceThenFails) {
    int calls = 0;
    EXPECT_FALSE(poll_until(
        [&] {
            ++calls;
            return false;
        },
        -5ms,
        BUSY_POLL_WINDOW,
        POLL_INTERVAL));
    EXPECT_EQ(calls, 1);
}

// The DRAM training waits throw from inside the predicate and rely on it reaching the caller.
TEST(PollUntil, PredicateExceptionPropagates) {
    EXPECT_THROW(
        poll_until([]() -> bool { throw std::runtime_error("boom"); }, TIMEOUT, BUSY_POLL_WINDOW, POLL_INTERVAL),
        std::runtime_error);
}

// Passing progress_what must not change what poll_until returns or how often it checks.
TEST(PollUntil, ProgressWhatDoesNotChangeResult) {
    int calls = 0;
    EXPECT_TRUE(poll_until([&] { return ++calls == 5; }, 1s, BUSY_POLL_WINDOW, POLL_INTERVAL, "something"));
    EXPECT_EQ(calls, 5);

    EXPECT_FALSE(poll_until([] { return false; }, TIMEOUT, BUSY_POLL_WINDOW, POLL_INTERVAL, "something"));

    EXPECT_THROW(
        poll_until(
            []() -> bool { throw std::runtime_error("boom"); }, TIMEOUT, BUSY_POLL_WINDOW, POLL_INTERVAL, "something"),
        std::runtime_error);
}

// Call sites pass fmt::format(...) results, so the string_view refers to a temporary that has to
// stay valid for the whole call.
TEST(PollUntil, ProgressWhatFromTemporaryString) {
    int calls = 0;
    EXPECT_TRUE(poll_until(
        [&] { return ++calls == 3; }, 1s, BUSY_POLL_WINDOW, POLL_INTERVAL, std::string("device ") + std::to_string(0)));
    EXPECT_EQ(calls, 3);
}
