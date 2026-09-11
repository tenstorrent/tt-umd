// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>
#include <sys/wait.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <initializer_list>
#include <thread>
#include <vector>

namespace tt::umd::test_utils {

// Reap pid within timeout_seconds, polling with WNOHANG so a stuck child can't wedge the parent.
// Returns true if the child was reaped (status is then filled in), false on timeout or if the child
// was already reaped by someone else.
inline bool wait_for_child(pid_t pid, int* status, int timeout_seconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid) {
            return true;
        }
        if (r == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// SIGKILLs the given children and reaps them. Used to clean up after a test gives up on a child.
inline void terminate_processes(const std::vector<pid_t>& pids) {
    for (pid_t p : pids) {
        if (p > 0) {
            kill(p, SIGKILL);
            // Bounded reap: a child wedged in uninterruptible (D) state ignores SIGKILL until it
            // leaves the kernel, so a blocking waitpid here would re-hang the parent. Give up after
            // ~5s and let the OS reap the orphan on exit rather than defeating the watchdog.
            for (int i = 0; i < 100 && waitpid(p, nullptr, WNOHANG) == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }
}

inline void terminate_processes(std::initializer_list<pid_t> pids) { terminate_processes(std::vector<pid_t>(pids)); }

}  // namespace tt::umd::test_utils
