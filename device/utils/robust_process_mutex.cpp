/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/utils/robust_process_mutex.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"

namespace tt::umd {

// RobustProcessMutex implementation
RobustProcessMutex::RobustProcessMutex(std::string_view mutex_name) : mutex_name_(mutex_name) {
    int err = pthread_mutex_init(&mutex_, nullptr);
    if (err != 0) {
        TT_THROW(
            fmt::format("pthread_mutex_init failed for process mutex {} errno: {}", mutex_name_, std::to_string(err)));
    }
}

RobustProcessMutex::~RobustProcessMutex() noexcept {
    int err = pthread_mutex_destroy(&mutex_);
    if (err != 0) {
        log_warning(
            tt::LogSiliconDriver,
            "pthread_mutex_destroy failed for process mutex {} errno: {}",
            mutex_name_,
            std::to_string(err));
    }
}

RobustProcessMutex::RobustProcessMutex(RobustProcessMutex&& other) noexcept :
    mutex_(other.mutex_), mutex_name_(std::move(other.mutex_name_)) {
    other.mutex_ = PTHREAD_MUTEX_INITIALIZER;
}

RobustProcessMutex& RobustProcessMutex::operator=(RobustProcessMutex&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        pthread_mutex_destroy(&mutex_);

        // Take ownership of other's resources
        mutex_ = other.mutex_;
        mutex_name_ = std::move(other.mutex_name_);

        // Reset other to safe state
        other.mutex_ = PTHREAD_MUTEX_INITIALIZER;
    }
    return *this;
}

void RobustProcessMutex::initialize() {
    // Mutex is already initialized in the constructor, nothing to do here
}

void RobustProcessMutex::lock() {
    int err = pthread_mutex_lock(&mutex_);
    if (err != 0) {
        TT_THROW(
            fmt::format("pthread_mutex_lock failed for process mutex {} errno: {}", mutex_name_, std::to_string(err)));
    }
}

void RobustProcessMutex::unlock() {
    int err = pthread_mutex_unlock(&mutex_);
    if (err != 0) {
        TT_THROW(fmt::format(
            "pthread_mutex_unlock failed for process mutex {} errno: {}", mutex_name_, std::to_string(err)));
    }
}
}  // namespace tt::umd
