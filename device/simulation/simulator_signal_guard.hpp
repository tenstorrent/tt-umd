// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>

namespace tt::umd {

// Runs a callback when the process receives SIGINT or SIGTERM, so a simulator the process spawned
// can be told to stop before the process dies. Without it, Ctrl+C on a hung app kills the app but
// leaves an emulation job holding the machine, because the job only stops on an explicit EXIT
// message that the (never-run) destructors would have sent.
//
// While at least one guard is alive, SIGINT and SIGTERM are routed to a handler that only writes
// the signal number to a self-pipe. A watcher thread reads it and, off the signal context, runs
// every live guard's callback, restores the handlers that were installed before the first guard,
// and re-sends the signal to the process, so whatever the app (or Python) would have done on that
// signal still happens. A signal the process was ignoring when the first guard was created stays
// ignored.
//
// Callbacks run on the watcher thread while other threads may be blocked mid-call, possibly
// holding locks, so a callback must not take locks a hung call could hold.
class SimulatorSignalGuard {
public:
    explicit SimulatorSignalGuard(std::function<void()> on_signal);
    ~SimulatorSignalGuard();

    SimulatorSignalGuard(const SimulatorSignalGuard &) = delete;
    SimulatorSignalGuard &operator=(const SimulatorSignalGuard &) = delete;

private:
    uint64_t id_;
};

}  // namespace tt::umd
