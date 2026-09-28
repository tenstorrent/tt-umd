// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <thread>

#include "simulation/simulator_signal_guard.hpp"

using namespace tt::umd;

// Every test raises a real signal, so each one runs its body in a death-test child: the signal
// and the handlers it installs never touch the test runner itself.

namespace {

void say(const char *text) { [[maybe_unused]] const ssize_t written = write(STDERR_FILENO, text, std::strlen(text)); }

// Waits for the re-sent signal to end the process. pause() returns each time a handler runs, which
// includes the guard's own handler before the watcher re-sends the signal.
[[noreturn]] void wait_for_signal() {
    while (true) {
        pause();
    }
}

void app_sigint_handler(int) {
    say("app handler\n");
    _exit(42);
}

class SimulatorSignalGuardTest : public ::testing::Test {
protected:
    // The guard starts a thread, which the default "fast" style (fork without exec) does not
    // survive reliably.
    void SetUp() override { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
};

}  // namespace

TEST_F(SimulatorSignalGuardTest, SigintRunsCallbackThenDefaultAction) {
    EXPECT_EXIT(
        {
            std::signal(SIGINT, SIG_DFL);
            SimulatorSignalGuard guard([] { say("callback ran\n"); });
            kill(getpid(), SIGINT);
            wait_for_signal();
        },
        testing::KilledBySignal(SIGINT),
        "callback ran");
}

TEST_F(SimulatorSignalGuardTest, SigtermRunsCallbackThenDefaultAction) {
    EXPECT_EXIT(
        {
            std::signal(SIGTERM, SIG_DFL);
            SimulatorSignalGuard guard([] { say("callback ran\n"); });
            kill(getpid(), SIGTERM);
            wait_for_signal();
        },
        testing::KilledBySignal(SIGTERM),
        "callback ran");
}

TEST_F(SimulatorSignalGuardTest, AppHandlerStillRunsAfterCallback) {
    EXPECT_EXIT(
        {
            std::signal(SIGINT, app_sigint_handler);
            SimulatorSignalGuard guard([] { say("callback ran\n"); });
            kill(getpid(), SIGINT);
            wait_for_signal();
        },
        testing::ExitedWithCode(42),
        "callback ran.*app handler");
}

TEST_F(SimulatorSignalGuardTest, EveryLiveGuardsCallbackRuns) {
    EXPECT_EXIT(
        {
            std::signal(SIGINT, SIG_DFL);
            SimulatorSignalGuard first([] { say("first ran\n"); });
            SimulatorSignalGuard second([] { say("second ran\n"); });
            kill(getpid(), SIGINT);
            wait_for_signal();
        },
        testing::KilledBySignal(SIGINT),
        "first ran.*second ran");
}

TEST_F(SimulatorSignalGuardTest, IgnoredSignalStaysIgnored) {
    EXPECT_EXIT(
        {
            std::signal(SIGINT, SIG_IGN);
            SimulatorSignalGuard guard([] { _exit(1); });
            kill(getpid(), SIGINT);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            _exit(0);
        },
        testing::ExitedWithCode(0),
        "");
}

TEST_F(SimulatorSignalGuardTest, DestroyingLastGuardRestoresAppHandler) {
    EXPECT_EXIT(
        {
            std::signal(SIGINT, app_sigint_handler);
            { SimulatorSignalGuard guard([] { _exit(1); }); }
            kill(getpid(), SIGINT);
            wait_for_signal();
        },
        testing::ExitedWithCode(42),
        "app handler");
}

TEST_F(SimulatorSignalGuardTest, DestroyedGuardsCallbackDoesNotRun) {
    EXPECT_EXIT(
        {
            std::signal(SIGINT, SIG_DFL);
            SimulatorSignalGuard kept([] { say("kept ran\n"); });
            { SimulatorSignalGuard dropped([] { _exit(1); }); }
            kill(getpid(), SIGINT);
            wait_for_signal();
        },
        testing::KilledBySignal(SIGINT),
        "kept ran");
}
