/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <pthread.h>

#include "umd/device/utils/robust_mutex.hpp"

namespace tt::umd {

// RobustProcessMutex provides a simple pthread mutex for single process use only.
// This is much faster than RobustSystemMutex but only works within a single process.
class RobustProcessMutex : public RobustMutex {
public:
    RobustProcessMutex(std::string_view mutex_name);
    ~RobustProcessMutex() noexcept;

    void initialize() override;

    // Move constructor and assignment
    RobustProcessMutex(RobustProcessMutex&& other) noexcept;
    RobustProcessMutex& operator=(RobustProcessMutex&& other) noexcept;

    // Copy constructor and assignment are not allowed.
    RobustProcessMutex(const RobustProcessMutex&) = delete;
    RobustProcessMutex& operator=(const RobustProcessMutex&) = delete;

    // Locks and unlocks the mutex.
    void unlock() override;
    void lock() override;

private:
    pthread_mutex_t mutex_;
    std::string mutex_name_;
};

}  // namespace tt::umd
