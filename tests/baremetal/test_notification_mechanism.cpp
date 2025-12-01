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

class WarmResetNotificationTest : public ::testing::Test {
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

TEST_F(WarmResetNotificationTest, MultiProcessTest) {
    auto constexpr NUM_CHILDREN = 5;
    std::vector<pid_t> child_pids;

    for (int i = 0; i < NUM_CHILDREN; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            int result = WarmResetNotificationTest::run_child_monitor_logic();
            _exit(result);
        }
        ASSERT_GT(pid, 0);
        child_pids.push_back(pid);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Notify
    WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(1000));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    WarmResetCommunication::Notifier::notify_all_listeners_post_reset();

    // Verify
    for (pid_t pid : child_pids) {
        int status;
        waitpid(pid, &status, 0);
        ASSERT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(WarmResetNotificationTest, NotifierIgnoresStaleSockets) {
    std::error_code ec;
    std::filesystem::create_directories(WarmResetCommunication::LISTENER_DIR, ec);

    // Create a fake socket file (just a regular empty file, or a bound socket with no listener)
    // Let's make it tricky: A file that looks like a socket name but is just a file.
    std::string fake_socket = std::string(WarmResetCommunication::LISTENER_DIR) + "/client_99999.sock";
    std::ofstream ofs(fake_socket);
    ofs.close();

    // Run Notifier
    // If code is fragile, this might throw an exception or hang.
    EXPECT_NO_THROW(
        { WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(100)); });

    EXPECT_NO_THROW({ WarmResetCommunication::Notifier::notify_all_listeners_post_reset(); });
}

TEST_F(WarmResetNotificationTest, ResilientToClientFailure) {
    pid_t good_pid = fork();
    if (good_pid == 0) {
        // This client behaves nicely
        _exit(run_child_monitor_logic());
    }

    pid_t bad_pid = fork();
    if (bad_pid == 0) {
        // This client starts monitoring but then crashes/exits immediately
        // leaving a valid socket file but no process reading it.
        WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        _exit(1);  // Die unexpectedly
    }

    // Give time for setup and for bad_pid to die
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // The Bad PID's socket is likely still there (OS cleanup might lag or file persists),
    // but connection will be refused. Notifier must survive.
    WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(500));
    WarmResetCommunication::Notifier::notify_all_listeners_post_reset();

    // Verify Good PID succeeded
    int status;
    waitpid(good_pid, &status, 0);
    EXPECT_EQ(WEXITSTATUS(status), 0);

    // Cleanup bad pid (already dead)
    waitpid(bad_pid, &status, 0);
}

TEST_F(WarmResetNotificationTest, MonitorCanRestart) {
    bool first_valid_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_TRUE(first_valid_start);

    bool double_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_FALSE(double_start);

    WarmResetCommunication::Monitor::stop_monitoring();

    // Allow a tiny moment for the detached thread to clean up
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    bool second_valid_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_TRUE(second_valid_start);

    WarmResetCommunication::Monitor::stop_monitoring();
}
