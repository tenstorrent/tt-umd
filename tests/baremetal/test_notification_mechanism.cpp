// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "device/api/umd/device/warm_reset.hpp"
#include "test_utils/pipe_communication.hpp"

using namespace tt;
using namespace tt::umd;

class WarmResetNotificationTest : public ::testing::Test {
public:
    static int run_child_monitor_logic(
        std::chrono::seconds process_pre_notification_wait_time = std::chrono::seconds(4),
        std::chrono::seconds process_post_notification_wait_time = std::chrono::seconds(4),
        const std::function<void()>& on_started = nullptr) {
        std::promise<void> pre_reset_promise;
        std::promise<void> post_reset_promise;
        auto pre_future = pre_reset_promise.get_future();
        auto post_future = post_reset_promise.get_future();

        bool success = WarmResetCommunication::Monitor::start_monitoring(
            [&]() { pre_reset_promise.set_value(); }, [&]() { post_reset_promise.set_value(); });

        if (!success) {
            return 1;
        }

        // Used only in WarmResetProcessWaitTest for testing unfulfilled promises.
        if (on_started) {
            on_started();
        }

        // Wait for PRE.
        if (pre_future.wait_for(process_pre_notification_wait_time) != std::future_status::ready) {
            return 101;  // Code 101: Pre Timeout
        }

        // Wait for POST.
        if (post_future.wait_for(process_post_notification_wait_time) != std::future_status::ready) {
            return 102;  // Code 102: Post Timeout
        }

        return 0;  // Success
    }

protected:
    void SetUp() override {
        // Clean the slate before every test.
        std::error_code ec;
        std::filesystem::remove_all(WarmResetCommunication::LISTENER_DIR, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(WarmResetCommunication::LISTENER_DIR, ec);
    }

    void wait_for_socket_state(int pid, bool should_exist) {
        std::string socket_name = "client_" + std::to_string(pid) + ".sock";
        std::filesystem::path socket_path = std::filesystem::path(WarmResetCommunication::LISTENER_DIR) / socket_name;

        int retries = 50;  // Wait up to 500ms.
        while (retries--) {
            bool currently_exists = std::filesystem::exists(socket_path);

            // If the current state matches the desired state, we are done.
            if (currently_exists == should_exist) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        FAIL() << "Timeout waiting for socket " << socket_path << " to "
               << (should_exist ? "appear (Creation)" : "vanish (Removal)");
    }
};

class WarmResetTimingTest : public WarmResetNotificationTest, public testing::WithParamInterface<int> {};

INSTANTIATE_TEST_SUITE_P(
    TimeoutScenarios,
    WarmResetTimingTest,
    ::testing::Values(
        100,  // Case 1: Fast (No Timeout)
        2000  // Case 2: Slow (Timeout)
        ),
    [](const testing::TestParamInfo<int>& info) {
        return info.param == 100 ? "FastSequence" : "SlowSequenceWithTimeout";
    });

TEST_P(WarmResetTimingTest, MultiProcess) {
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

    // Allow startup of processes.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::chrono::milliseconds sleep_duration_ms = std::chrono::milliseconds(GetParam());

    WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(1000));
    std::this_thread::sleep_for(sleep_duration_ms);
    WarmResetCommunication::Notifier::notify_all_listeners_post_reset();

    // Verify.
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
        // This client behaves nicely.
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

    // Give time for setup and for bad_pid to die.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // The Bad PID's socket is likely still there (OS cleanup might lag or file persists),
    // but connection will be refused. Notifier must survive.
    WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(500));
    WarmResetCommunication::Notifier::notify_all_listeners_post_reset();

    // Verify Good PID succeeded.
    int status;
    waitpid(good_pid, &status, 0);
    EXPECT_EQ(WEXITSTATUS(status), 0);

    // Cleanup bad pid (already dead).
    waitpid(bad_pid, &status, 0);
}

TEST_F(WarmResetNotificationTest, MonitorCanRestart) {
    bool first_valid_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_TRUE(first_valid_start);

    wait_for_socket_state(getpid(), true);

    bool double_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_FALSE(double_start);

    WarmResetCommunication::Monitor::stop_monitoring();

    wait_for_socket_state(getpid(), false);

    bool second_valid_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_TRUE(second_valid_start);

    wait_for_socket_state(getpid(), true);

    WarmResetCommunication::Monitor::stop_monitoring();
}

struct TimeoutParams {
    std::chrono::milliseconds pre_wait;
    std::chrono::milliseconds post_wait;
    int expected_rc;
    bool should_trigger_pre;  // Needed to reach the "Post" check
};

class WarmResetProcessWaitTest : public WarmResetNotificationTest, public testing::WithParamInterface<TimeoutParams> {
    void TearDown() override {
        WarmResetCommunication::Monitor::stop_monitoring();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        WarmResetNotificationTest::TearDown();
    }
};

INSTANTIATE_TEST_SUITE_P(
    TimeoutScenarios,
    WarmResetProcessWaitTest,
    ::testing::Values(
        // Case 1: Pre Timeout (101).
        // Wait 1ms for Pre. Don't send signal. Fails immediately.
        TimeoutParams{std::chrono::milliseconds(1), std::chrono::seconds(1), 101, false},

        // Case 2: Post Timeout (102).
        // Wait 2s for Pre (success), 1ms for Post (fail). Send Pre signal only.
        TimeoutParams{std::chrono::seconds(2), std::chrono::milliseconds(1), 102, true}),
    [](const testing::TestParamInfo<TimeoutParams>& info) {
        return info.param.expected_rc == 101 ? "PreTimeout_101" : "PostTimeout_102";
    });

TEST_P(WarmResetProcessWaitTest, ValidatesTimeoutLogic) {
    auto params = GetParam();
    tt::umd::test_utils::MultiProcessPipe pipe(1);

    pid_t pid = fork();

    if (pid == 0) {
        int result = run_child_monitor_logic(
            std::chrono::duration_cast<std::chrono::seconds>(params.pre_wait),
            std::chrono::duration_cast<std::chrono::seconds>(params.post_wait),
            [&]() { pipe.signal_ready_from_child(0); });
        _exit(result);
    }

    ASSERT_TRUE(pipe.wait_for_all_children(5));

    if (params.should_trigger_pre) {
        // SAFETY CHECK: Ensure the background thread actually created the socket.
        // This is much better than a hardcoded sleep.
        std::string socket_path =
            std::string(WarmResetCommunication::LISTENER_DIR) + "/client_" + std::to_string(pid) + ".sock";

        // Quick spin-wait (usually exits instantly).
        while (!std::filesystem::exists(socket_path)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Now we are 100% sure the listener is active.
        WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(500));
    }

    int status;
    waitpid(pid, &status, 0);

    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), params.expected_rc);
}
