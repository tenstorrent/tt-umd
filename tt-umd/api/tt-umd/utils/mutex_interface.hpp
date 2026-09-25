// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>  // pid_t

#include <chrono>
#include <optional>
#include <utility>

namespace tt::umd {

// Common interface of UMD's cross-process locking backends, so that a lock can be guarded by whichever
// backend suits it without its users having to know which one. See device/utils/README.md for how the
// backends compare.
// Implementations meet the C++ BasicLockable requirement, so they work with std::lock_guard and
// std::unique_lock.
//
// What an implementation owes its callers, and what it does not:
//   - The mutex is not recursive. A thread already holding it must not take it again.
//   - Misuse is not required to be diagnosed. Taking the lock again from the thread that holds it,
//     unlocking from a thread that does not hold it, or unlocking twice, may throw, may be ignored, or
//     may block forever. Which of those happens is not part of this interface, so callers must not
//     depend on any of them. Taking the lock through std::lock_guard or std::unique_lock avoids all
//     three.
class MutexInterface {
public:
    MutexInterface() = default;
    virtual ~MutexInterface() = default;

    // Copying through a MutexInterface& would slice, and an implementation holding an OS resource
    // cannot be copied meaningfully anyway. Deleting these in the implementations does not cover an
    // assignment made through the interface, which is what callers hold.
    MutexInterface(const MutexInterface&) = delete;
    MutexInterface& operator=(const MutexInterface&) = delete;

    // Sets up the underlying OS resource. Must be called before any locking operation.
    virtual void initialize() = 0;

    virtual void lock() = 0;

    virtual void unlock() = 0;

    // Tries to acquire the lock, waiting up to timeout. Returns std::nullopt if the lock was acquired,
    // otherwise the owning {pid, tid} if the backend can identify the owner, {0, 0} if it cannot.
    virtual std::optional<std::pair<pid_t, pid_t>> probe_lock(std::chrono::seconds timeout) = 0;
};

}  // namespace tt::umd
