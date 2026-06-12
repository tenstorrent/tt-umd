// SPDX-FileCopyrightText: © 2026 Sungjoon Moon
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <thread>

#include "umd/device/utils/robust_mutex.hpp"

using tt::umd::RobustMutex;

static constexpr const char* TEST_MUTEX_NAME = "TEST_SIGINT";

static void noop_handler(int) {}

class RobustMutexSigintTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_unlink(("TT_UMD_LOCK." + std::string(TEST_MUTEX_NAME)).c_str());
        struct sigaction sa = {};
        sa.sa_handler = noop_handler;
        sigemptyset(&sa.sa_mask);
        ASSERT_EQ(sigaction(SIGINT, &sa, &old_action_), 0);
    }

    void TearDown() override {
        ASSERT_EQ(sigaction(SIGINT, &old_action_, nullptr), 0);
        shm_unlink(("TT_UMD_LOCK." + std::string(TEST_MUTEX_NAME)).c_str());
    }

    struct sigaction old_action_ = {};
};

TEST_F(RobustMutexSigintTest, NormalLockSucceeds) {
    RobustMutex m(TEST_MUTEX_NAME);
    m.initialize();
    m.lock();
    m.unlock();
}

TEST_F(RobustMutexSigintTest, SigintAbortsPendingLock) {
    RobustMutex holder(TEST_MUTEX_NAME);
    holder.initialize();
    holder.lock();

    std::thread waiter([&] {
        RobustMutex m(TEST_MUTEX_NAME);
        m.initialize();
        EXPECT_THROW(m.lock(), std::exception);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    kill(getpid(), SIGINT);

    waiter.join();
    holder.unlock();
}

TEST_F(RobustMutexSigintTest, SigintWithChildProcess) {
    int ready_pipe[2];
    int done_pipe[2];
    ASSERT_EQ(pipe(ready_pipe), 0);
    ASSERT_EQ(pipe(done_pipe), 0);

    pid_t pid = fork();
    ASSERT_NE(pid, -1);

    if (pid == 0) {
        if (close(ready_pipe[0]) != 0) {
            _exit(10);
        }
        if (close(done_pipe[1]) != 0) {
            _exit(11);
        }

        RobustMutex m(TEST_MUTEX_NAME);
        m.initialize();
        m.lock();

        char ready = 1;
        if (write(ready_pipe[1], &ready, 1) != 1) {
            _exit(12);
        }
        if (close(ready_pipe[1]) != 0) {
            _exit(13);
        }

        char buf;
        if (read(done_pipe[0], &buf, 1) < 0) {
            _exit(14);
        }
        m.unlock();
        if (close(done_pipe[0]) != 0) {
            _exit(15);
        }
        _exit(0);
    }

    ASSERT_EQ(close(ready_pipe[1]), 0);
    ASSERT_EQ(close(done_pipe[0]), 0);

    char ready;
    ASSERT_EQ(read(ready_pipe[0], &ready, 1), 1);
    ASSERT_EQ(close(ready_pipe[0]), 0);

    RobustMutex m(TEST_MUTEX_NAME);
    m.initialize();

    std::thread waiter([&] { EXPECT_THROW(m.lock(), std::exception); });

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    kill(getpid(), SIGINT);

    waiter.join();

    ASSERT_EQ(close(done_pipe[1]), 0);
    int status;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
