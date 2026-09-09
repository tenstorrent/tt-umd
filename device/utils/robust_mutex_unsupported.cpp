// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <utility>

#include "umd/device/utils/error.hpp"
#include "umd/device/utils/robust_mutex.hpp"

namespace tt::umd {

// macOS has no process-shared robust pthread mutexes. Simulator locks are
// process-local; hardware locks must fail rather than lose owner-death recovery.
RobustMutex::RobustMutex(std::string_view name) : mutex_name_(name) {}

RobustMutex::~RobustMutex() noexcept = default;

RobustMutex::RobustMutex(RobustMutex&& other) noexcept : mutex_name_(std::move(other.mutex_name_)) {}

RobustMutex& RobustMutex::operator=(RobustMutex&& other) noexcept {
    mutex_name_ = std::move(other.mutex_name_);
    return *this;
}

void RobustMutex::initialize() {
    UMD_THROW(error::RuntimeError, "Process-shared robust device locks are unavailable on macOS; use the simulator.");
}

void RobustMutex::lock() { initialize(); }

void RobustMutex::unlock() { initialize(); }

std::optional<std::pair<pid_t, pid_t>> RobustMutex::probe_lock(std::chrono::seconds) {
    initialize();
    return std::nullopt;
}

}  // namespace tt::umd
