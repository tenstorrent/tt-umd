// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <vector>

#include "pipe_communication.hpp"

using namespace tt::umd::test_utils;

TEST(MultiProcessPipeTest, ParentWaitsForMultipleChildren) {
    constexpr int num_children = 3;
    MultiProcessPipe pipe(num_children);
    std::vector<pid_t> child_pids;

    for (int i = 0; i < num_children; ++i) {
        pid_t pid = fork();

        // The fork return value gives info if it's a child process (pid == 0 means it is).
        if (pid == 0) {
            // Sleep different amounts to prove we wait for the SLOWEST child
            // Child 0 sleeps 0ms, Child 1 sleeps 10ms, Child 2 sleeps 20ms.
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 10));

            // Signal specific index.
            pipe.signal_ready_from_child(i);

            _exit(0);
        }

        // Parent tracks the PID.
        child_pids.push_back(pid);
    }

    bool success = pipe.wait_for_all_children(1);  // 1 second timeout
    EXPECT_TRUE(success) << "Parent process failed to synchronize with all " << num_children << " child processes";

    // Clean up all zombie processes.
    for (pid_t pid : child_pids) {
        waitpid(pid, nullptr, 0);
    }
}

TEST(MultiProcessPipeTest, ParentTimesOutIfChildIsSilent) {
    MultiProcessPipe pipe(1);

    pid_t pid = fork();

    if (pid == 0) {
        // Sleep longer than the parent's timeout.
        std::this_thread::sleep_for(std::chrono::seconds(2));
        _exit(0);
    }

    // Wait only 1 second (Child sleeps for 2s).
    bool success = pipe.wait_for_all_children(1);
    EXPECT_FALSE(success) << "Parent should have timed out, but didn't";

    // Clean up zombie process.
    waitpid(pid, nullptr, 0);
}

TEST(MultiProcessPipeTest, PartialSuccessIsFailure) {
    const int num_children = 3;
    MultiProcessPipe pipe(num_children);
    std::vector<pid_t> child_pids;

    for (int i = 0; i < num_children; ++i) {
        pid_t pid = fork();

        if (pid == 0) {
            if (i % 2 == 1) {
                // Odd child processes won't signal the parent process.
                // They sleep longer than the timeout (simulating a hang or crash).
                std::this_thread::sleep_for(std::chrono::seconds(2));
            } else {
                // Even child processes signal immediately.
                pipe.signal_ready_from_child(i);
            }

            _exit(0);
        }
        child_pids.push_back(pid);
    }

    // Timeout is 1 second. Even child processes (0, 2) signal instantly,
    // but odd child process (1) won't signal in time, so the result must be false.
    bool success = pipe.wait_for_all_children(1);

    EXPECT_FALSE(success) << "Should fail because odd child processes did not signal in time";

    // Clean up all zombie processes.
    for (pid_t pid : child_pids) {
        waitpid(pid, nullptr, 0);
    }
}
