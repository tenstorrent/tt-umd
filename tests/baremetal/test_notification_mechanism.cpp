// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>

#include "device/api/umd/device/warm_reset.hpp"

using namespace tt;
using namespace tt::umd;

class WarnResetNotificationTest : public ::testing::Test {
public:
    static int run_child_monitor_logic() {
        std::promise<void> pre_reset_promise;
        std::promise<void> post_reset_promise;
        auto pre_future = pre_reset_promise.get_future();
        auto post_future = post_reset_promise.get_future();

        bool success = WarmResetCommunication::Monitor::start_monitoring(
            [&]() { pre_reset_promise.set_value(); }, [&]() { post_reset_promise.set_value(); });

        if (!success) {
            return 1;
        }

        // Wait for PRE
        if (pre_future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            return 101;  // Code 101: Pre Timeout
        }

        // Wait for POST
        if (post_future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            return 102;  // Code 102: Post Timeout
        }

        return 0;  // Success
    }

protected:
    void SetUp() override {
        // Clean the slate before every test
        std::error_code ec;
        std::filesystem::remove_all(WarmResetCommunication::LISTENER_DIR, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(WarmResetCommunication::LISTENER_DIR, ec);
    }
};
