// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulator_signal_guard.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

namespace tt::umd {

namespace {

constexpr std::array<int, 2> kSignals = {SIGINT, SIGTERM};

// Written to the pipe to make the watcher thread return. No signal has number 0.
constexpr unsigned char kStopWatcher = 0;

// Read by the signal handler, so these two are atomics rather than members of GuardState.
std::atomic<int> g_pipe_write{-1};
std::atomic<pid_t> g_owner_pid{-1};

struct GuardState {
    std::mutex mutex;
    std::map<uint64_t, std::function<void()>> callbacks;
    uint64_t next_id = 0;

    bool handlers_installed = false;
    std::array<struct sigaction, kSignals.size()> previous{};
    std::array<bool, kSignals.size()> handled{};

    std::thread watcher;
    int pipe_read = -1;
};

// Leaked on purpose: a signal can arrive while static destructors are running at exit.
GuardState &state() {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    static auto *s = new GuardState();
    return *s;
}

void handle_signal(int sig) {
    const int saved_errno = errno;
    if (getpid() != g_owner_pid.load()) {
        // A fork()ed child inherited the handler but not the watcher thread, and the pipe it would
        // write to still wakes the parent's watcher. Die the default way instead.
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    } else if (const int fd = g_pipe_write.load(); fd >= 0) {
        const auto byte = static_cast<unsigned char>(sig);
        [[maybe_unused]] const ssize_t written = write(fd, &byte, 1);
    }
    errno = saved_errno;
}

// Caller holds state().mutex.
void install_handlers(GuardState &s) {
    for (size_t i = 0; i < kSignals.size(); i++) {
        sigaction(kSignals[i], nullptr, &s.previous[i]);
        const bool ignored = !(s.previous[i].sa_flags & SA_SIGINFO) && s.previous[i].sa_handler == SIG_IGN;
        s.handled[i] = !ignored;
        if (ignored) {
            continue;
        }
        struct sigaction action {};
        action.sa_handler = handle_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction(kSignals[i], &action, nullptr);
    }
    s.handlers_installed = true;
}

// Caller holds state().mutex.
void restore_handlers(GuardState &s) {
    for (size_t i = 0; i < kSignals.size(); i++) {
        if (s.handled[i]) {
            sigaction(kSignals[i], &s.previous[i], nullptr);
        }
    }
    s.handlers_installed = false;
}

void watcher_loop(int read_fd) {
    GuardState &s = state();
    while (true) {
        unsigned char byte = 0;
        const ssize_t n = read(read_fd, &byte, 1);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0 || byte == kStopWatcher) {
            return;
        }
        const int sig = byte;

        {
            std::lock_guard<std::mutex> lock(s.mutex);
            // A second signal that arrived while the first was being handled finds the handlers
            // already restored; the re-sent first signal owns what happens next.
            if (!s.handlers_installed) {
                continue;
            }
            log_warning(
                tt::LogEmulationDriver, "Received signal {} ({}), stopping the simulator.", sig, strsignal(sig));
            // Run under the lock so a guard cannot be destroyed, and the object its callback
            // points at freed, while the callback runs.
            for (auto &[id, callback] : s.callbacks) {
                try {
                    callback();
                } catch (const std::exception &e) {
                    log_error(tt::LogEmulationDriver, "Stopping the simulator on signal {} failed: {}", sig, e.what());
                }
            }
            restore_handlers(s);
        }

        // Process-directed, so the app's own handler (or the default action) sees the signal as if
        // we had never been installed.
        kill(getpid(), sig);
    }
}

}  // namespace

SimulatorSignalGuard::SimulatorSignalGuard(std::function<void()> on_signal) {
    GuardState &s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    id_ = s.next_id++;
    s.callbacks.emplace(id_, std::move(on_signal));

    if (!s.watcher.joinable()) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) {
            log_warning(
                tt::LogEmulationDriver,
                "Could not create the signal pipe ({}); the simulator will not be stopped on Ctrl+C.",
                std::strerror(errno));
            return;
        }
        // The handler must never block on a full pipe; one queued byte is enough to wake the watcher.
        fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
        s.pipe_read = fds[0];
        g_owner_pid.store(getpid());
        g_pipe_write.store(fds[1]);
        s.watcher = std::thread(watcher_loop, fds[0]);
    }
    if (!s.handlers_installed) {
        install_handlers(s);
    }
}

SimulatorSignalGuard::~SimulatorSignalGuard() {
    GuardState &s = state();
    std::thread watcher;
    int read_fd = -1;
    int write_fd = -1;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.callbacks.erase(id_);
        if (!s.callbacks.empty()) {
            return;
        }
        if (s.handlers_installed) {
            restore_handlers(s);
        }
        // Handlers are restored before the pipe is retired, so no handler writes to a closed fd.
        write_fd = g_pipe_write.exchange(-1);
        read_fd = std::exchange(s.pipe_read, -1);
        watcher = std::move(s.watcher);
    }

    // Joined outside the lock: the watcher may be waiting on it to handle a signal queued just
    // before the handlers were restored.
    if (watcher.joinable()) {
        [[maybe_unused]] const ssize_t written = write(write_fd, &kStopWatcher, 1);
        watcher.join();
    }
    if (read_fd >= 0) {
        close(read_fd);
    }
    if (write_fd >= 0) {
        close(write_fd);
    }
}

}  // namespace tt::umd
