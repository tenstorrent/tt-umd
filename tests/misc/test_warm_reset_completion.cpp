// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "warm_reset_completion.hpp"

using namespace tt::umd;
using namespace std::chrono_literals;

namespace {

constexpr auto REAPPEAR_TIMEOUT = std::chrono::milliseconds(50);
constexpr auto MARKER_TIMEOUT = std::chrono::milliseconds(50);

const std::map<int, std::string> FOUR_CHIPS = {
    {0, "0000:01:00.0"}, {1, "0000:02:00.0"}, {2, "0000:03:00.0"}, {3, "0000:04:00.0"}};

// Fakes the host after a reset: chips in `gone` never reappear, chips in `stuck_marker` reappear but keep their
// reset marker set, as a chip does when it hangs during the reset. Records what complete_warm_reset asks for.
struct FakeHost {
    std::set<std::string> gone;
    std::set<std::string> stuck_marker;
    std::vector<std::string> marker_checks;
    std::vector<std::unordered_set<int>> post_resets;

    std::vector<std::string> complete(const std::map<int, std::string>& devices) {
        return complete_warm_reset(
            devices,
            REAPPEAR_TIMEOUT,
            MARKER_TIMEOUT,
            [this](const std::string& bdf, std::chrono::milliseconds) { return gone.count(bdf) == 0; },
            [this](const std::string& bdf, std::chrono::milliseconds) {
                marker_checks.push_back(bdf);
                return stuck_marker.count(bdf) == 0;
            },
            [this](const std::unordered_set<int>& ids) { post_resets.push_back(ids); });
    }
};

}  // namespace

TEST(WarmResetCompletion, AllChipsBackGetPostReset) {
    FakeHost host;

    EXPECT_TRUE(host.complete(FOUR_CHIPS).empty());
    ASSERT_EQ(host.post_resets.size(), 1);
    EXPECT_EQ(host.post_resets[0], (std::unordered_set<int>{0, 1, 2, 3}));
}

// The bug this guards: the first chip whose marker never clears ended the reset, so the chips after it were never
// checked and no chip got POST_RESET, which left every chip unusable.
TEST(WarmResetCompletion, StuckMarkerOnFirstChipStillPostResetsTheOthers) {
    FakeHost host;
    host.stuck_marker = {"0000:01:00.0"};

    EXPECT_EQ(host.complete(FOUR_CHIPS), (std::vector<std::string>{"0000:01:00.0"}));
    EXPECT_EQ(
        host.marker_checks, (std::vector<std::string>{"0000:01:00.0", "0000:02:00.0", "0000:03:00.0", "0000:04:00.0"}));
    ASSERT_EQ(host.post_resets.size(), 1);
    EXPECT_EQ(host.post_resets[0], (std::unordered_set<int>{1, 2, 3}));
}

TEST(WarmResetCompletion, ChipThatNeverReappearsIsReportedAndSkipped) {
    FakeHost host;
    host.gone = {"0000:03:00.0"};

    EXPECT_EQ(host.complete(FOUR_CHIPS), (std::vector<std::string>{"0000:03:00.0"}));
    EXPECT_EQ(host.marker_checks, (std::vector<std::string>{"0000:01:00.0", "0000:02:00.0", "0000:04:00.0"}));
    ASSERT_EQ(host.post_resets.size(), 1);
    EXPECT_EQ(host.post_resets[0], (std::unordered_set<int>{0, 1, 3}));
}

TEST(WarmResetCompletion, EveryFailedChipIsNamed) {
    FakeHost host;
    host.gone = {"0000:04:00.0"};
    host.stuck_marker = {"0000:02:00.0"};

    EXPECT_EQ(host.complete(FOUR_CHIPS), (std::vector<std::string>{"0000:04:00.0", "0000:02:00.0"}));
    ASSERT_EQ(host.post_resets.size(), 1);
    EXPECT_EQ(host.post_resets[0], (std::unordered_set<int>{0, 2}));
}

// An empty POST_RESET target set means every device on the host, so it must never be sent.
TEST(WarmResetCompletion, NoChipBackSendsNoPostReset) {
    FakeHost host;
    host.stuck_marker = {"0000:01:00.0", "0000:02:00.0"};
    host.gone = {"0000:03:00.0", "0000:04:00.0"};

    EXPECT_EQ(
        host.complete(FOUR_CHIPS),
        (std::vector<std::string>{"0000:03:00.0", "0000:04:00.0", "0000:01:00.0", "0000:02:00.0"}));
    EXPECT_TRUE(host.post_resets.empty());
}

// A chip that uses up the whole marker wait leaves no wait for the next chip, which is still checked once,
// so a dead chip delays the reset by one timeout no matter how many chips follow it.
TEST(WarmResetCompletion, DeadChipSpendsTheSharedDeadlineOnce) {
    std::vector<std::chrono::milliseconds> marker_timeouts;
    const auto start = std::chrono::steady_clock::now();

    const auto failed = complete_warm_reset(
        {{0, "0000:01:00.0"}, {1, "0000:02:00.0"}, {2, "0000:03:00.0"}},
        REAPPEAR_TIMEOUT,
        MARKER_TIMEOUT,
        [](const std::string&, std::chrono::milliseconds) { return true; },
        [&](const std::string& bdf, std::chrono::milliseconds timeout) {
            marker_timeouts.push_back(timeout);
            if (bdf == "0000:01:00.0") {
                std::this_thread::sleep_for(timeout);
                return false;
            }
            return true;
        },
        [](const std::unordered_set<int>&) {});

    EXPECT_EQ(failed, (std::vector<std::string>{"0000:01:00.0"}));
    ASSERT_EQ(marker_timeouts.size(), 3);
    EXPECT_GT(marker_timeouts[0], 0ms);
    EXPECT_EQ(marker_timeouts[1], 0ms);
    EXPECT_EQ(marker_timeouts[2], 0ms);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2 * MARKER_TIMEOUT);
}
