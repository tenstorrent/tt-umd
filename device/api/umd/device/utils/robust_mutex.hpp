/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace tt::umd {

enum class MutexImplementationType {
    SYSTEM_WIDE,   // Uses shared memory, survives process crashes, inter-process synchronization
    PROCESS_LOCAL  // Simple pthread mutex, single process only, better performance
};

// RobustMutex is an interface that provides a robust locking mechanism.
// It meets BasicLockable requirement, so it can be used with C++ standard library RAII lock classes like lock_guard and
// unique_lock.
class RobustMutex {
public:
    static std::unique_ptr<RobustMutex> create(std::string_view mutex_name, MutexImplementationType type);

    virtual ~RobustMutex() = default;

    // Does everything related to initializing the mutex, even on first time creation.
    // The initialization can fail and throw an exception.
    virtual void initialize() = 0;

    // Locks and unlocks the mutex.
    virtual void unlock() = 0;
    virtual void lock() = 0;
};

}  // namespace tt::umd
