/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// ASan and TSan often fail with siglongjmp because the jump bypasses
// stack unwinding/poisoning updates that the sanitizers rely on.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define IS_SANITIZER_ACTIVE 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define IS_SANITIZER_ACTIVE 1
#endif
#endif

#ifndef IS_SANITIZER_ACTIVE
#define IS_SANITIZER_ACTIVE 0
#endif

static thread_local sigjmp_buf point;
static thread_local std::atomic<bool> jump_set = false;

void sigbus_handler(int sig) {
    if (jump_set) {
        siglongjmp(point, 1);
    } else {
        _exit(sig);
    }
}

struct ScopedJumpGuard {
    ScopedJumpGuard() {
        jump_set.store(true);
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    ~ScopedJumpGuard() {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        jump_set.store(false);
    }
};

class TTDeviceSafeDummy {
public:
    static void setup_signal_handler() {
        struct sigaction sa;
        sa.sa_handler = sigbus_handler;
        sigemptyset(&sa.sa_mask);
        // SA_NODEFER: Don't block SIGBUS after we longjmp out
        sa.sa_flags = SA_NODEFER;

        if (sigaction(SIGBUS, &sa, nullptr) == -1) {
            perror("sigaction");
            _exit(1);
        }
    }

    void safe_execute(std::function<void()> operation) {
        if (sigsetjmp(point, 1) == 0) {
            ScopedJumpGuard guard;
            operation();
        } else {
            jump_set.store(false);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            throw std::runtime_error("SIGBUS");
        }
    }
};

class SigBusMechanismTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (IS_SANITIZER_ACTIVE) {
            GTEST_SKIP() << "Skipping SIGBUS tests: Incompatible with Address/Thread Sanitizer (ASan/TSan)";
        }
        jump_set.store(false);
        TTDeviceSafeDummy::setup_signal_handler();
    }

    void TearDown() override { signal(SIGBUS, SIG_DFL); }
};

TEST_F(SigBusMechanismTest, NoSigBus) {
    TTDeviceSafeDummy device;
    bool executed = false;

    EXPECT_NO_THROW({ device.safe_execute([&]() { executed = true; }); });

    EXPECT_TRUE(executed);
    EXPECT_FALSE(jump_set);
}

TEST_F(SigBusMechanismTest, HandleSigBus) {
    TTDeviceSafeDummy device;

    EXPECT_THROW(
        {
            device.safe_execute([]() {
                std::raise(SIGBUS);

                FAIL() << "Execution continued after raise(SIGBUS)";
            });
        },
        std::runtime_error);

    EXPECT_FALSE(jump_set);
}

TEST_F(SigBusMechanismTest, HandleCppException) {
    TTDeviceSafeDummy device;

    EXPECT_THROW({ device.safe_execute([]() { throw std::logic_error("Normal logic error"); }); }, std::logic_error);

    EXPECT_FALSE(jump_set);
}

TEST_F(SigBusMechanismTest, ThreadIsolation) {
    std::atomic<int> success_count{0};

    auto thread_work = [&](int id) {
        TTDeviceSafeDummy device;
        try {
            if (id % 2 == 0) {
                // Even threads crash.
                device.safe_execute([]() { std::raise(SIGBUS); });
            } else {
                // Odd threads succeed.
                device.safe_execute([]() { /* do nothing */ });
            }
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()) == "SIGBUS") {
                success_count++;
            }
        } catch (...) {
            // Should not happen for odd threads.
        }

        if (id % 2 != 0) {
            success_count++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(thread_work, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    // 5 threads caught SIGBUS, 5 threads finished normally = 10.
    EXPECT_EQ(success_count, 10);
}

// Spawns multiple child processes, each spawning multiple threads.
// Threads execute in random-ish order (via scheduling) and randomly crash or succeed.
TEST_F(SigBusMechanismTest, MultiProcessMultiThreadStress) {
    const int NUM_PROCESSES = 4;
    const int NUM_THREADS_PER_PROCESS = 10;
    std::vector<pid_t> children;

    for (int p = 0; p < NUM_PROCESSES; ++p) {
        pid_t pid = fork();
        ASSERT_GE(pid, 0) << "Fork failed";

        if (pid == 0) {
            // In a forked child, we run the stress test independently.
            // We exit with 0 on success, 1 on failure.
            std::atomic<int> success_count{0};
            std::atomic<int> failure_count{0};

            auto thread_work = [&](int id) {
                // Using simple modulo logic to avoid complex random dependencies.
                std::this_thread::sleep_for(std::chrono::milliseconds((id * 7) % 10));

                TTDeviceSafeDummy device;
                try {
                    if (id % 2 == 0) {
                        // Even threads trigger SIGBUS
                        device.safe_execute([]() { std::raise(SIGBUS); });
                    } else {
                        // Odd threads run normally
                        device.safe_execute([]() { /* Happy path */ });
                    }
                } catch (const std::runtime_error& e) {
                    // Even threads should catch SIGBUS
                    if (id % 2 == 0 && std::string(e.what()) == "SIGBUS") {
                        success_count++;
                    } else {
                        failure_count++;
                    }
                } catch (...) {
                    failure_count++;
                }

                // Odd threads should finish without throwing.
                if (id % 2 != 0) {
                    success_count++;
                }
            };

            std::vector<std::thread> threads;
            for (int t = 0; t < NUM_THREADS_PER_PROCESS; ++t) {
                threads.emplace_back(thread_work, t);
            }

            for (auto& t : threads) {
                t.join();
            }

            // Check if all threads behaved as expected.
            if (success_count == NUM_THREADS_PER_PROCESS && failure_count == 0) {
                std::exit(0);
            } else {
                std::cerr << "Process " << getpid() << " failed: Success=" << success_count
                          << ", Fail=" << failure_count << std::endl;
                std::exit(1);
            }
        } else {
            // Parent: store child PID.
            children.push_back(pid);
        }
    }

    // Wait for all children to complete and verify their exit codes.
    for (pid_t pid : children) {
        int status;
        waitpid(pid, &status, 0);

        // Ensure child exited normally (WIFEXITED) and with success code 0 (WEXITSTATUS).
        EXPECT_TRUE(WIFEXITED(status)) << "Child process " << pid << " crashed or was killed.";
        EXPECT_EQ(WEXITSTATUS(status), 0) << "Child process " << pid << " reported test failure.";
    }
}

TEST_F(SigBusMechanismTest, CrashIfHandlerNotSet) {
    TTDeviceSafeDummy device;

    // Manually remove the handler for this specific test.
    signal(SIGBUS, SIG_DFL);

    // This checks that the process actually dies with signal SIGBUS
    // regex ".*" matches any output to stderr.
    ASSERT_DEATH(
        {
            // Even though safe_execute does sigsetjmp, the OS doesn't know to call
            // our handler, so it defaults to terminating the process.
            device.safe_execute([]() { std::raise(SIGBUS); });
        },
        "");
}
