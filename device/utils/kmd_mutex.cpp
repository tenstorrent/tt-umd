// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/kmd_mutex.hpp"

#include <fcntl.h>  // O_APPEND
#include <fmt/format.h>

#include <cerrno>
#include <chrono>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

KmdMutex::KmdMutex(int pci_device_num, uint8_t lock_index) :
    pci_device_num_(pci_device_num),
    lock_index_(lock_index),
    device_path_(fmt::format("/dev/tenstorrent/{}", pci_device_num)) {
    UMD_ASSERT(
        lock_index_ < TT_RESOURCE_LOCK_COUNT,
        error::RuntimeError,
        fmt::format("KMD resource lock index {} out of range [0, {})", lock_index_, TT_RESOURCE_LOCK_COUNT));
}

KmdMutex::~KmdMutex() noexcept { close_device(); }

KmdMutex::KmdMutex(KmdMutex&& other) noexcept :
    pci_device_num_(other.pci_device_num_),
    lock_index_(other.lock_index_),
    device_(other.device_),
    device_path_(std::move(other.device_path_)) {
    // Invalidate the other object so its destructor doesn't close our handle.
    other.device_ = nullptr;
}

KmdMutex& KmdMutex::operator=(KmdMutex&& other) noexcept {
    if (this != &other) {
        close_device();  // clean up existing resources

        pci_device_num_ = other.pci_device_num_;
        lock_index_ = other.lock_index_;
        device_ = other.device_;
        device_path_ = std::move(other.device_path_);

        other.device_ = nullptr;
    }
    return *this;
}

void KmdMutex::open_device() {
    // O_APPEND tells KMD (>= 2.6) not to request high power on open; a lock-only handle should not
    // change the device's power state. The flag is harmless on older KMD versions.
    int result = tt_device_open(device_path_.c_str(), &device_, O_APPEND);
    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_device_open() failed for KMD lock device {} errno: {}", device_path_, -result));
}

void KmdMutex::initialize() { open_device(); }

void KmdMutex::close_device() noexcept {
    if (device_ != nullptr) {
        // Closing the handle releases any lock held on it (enforced by KMD), so this is also our
        // unlock path on destruction.
        int result = tt_device_close(device_);
        if (result != 0) {
            // Destructor path: log instead of throwing.
            log_warning(tt::LogUMD, "tt_device_close() failed for KMD lock device {} errno: {}", device_path_, -result);
        }
        device_ = nullptr;
    }
}

bool KmdMutex::try_lock() {
    UMD_ASSERT(device_ != nullptr, error::RuntimeError, "KmdMutex::try_lock() called before initialize()");

    int acquired = 0;
    int result = tt_lock_acquire(device_, lock_index_, &acquired);

    // A reset across our handle makes it unusable; reopen and retry once on a fresh handle.
    if (result == -ENODEV) {
        close_device();
        open_device();
        result = tt_lock_acquire(device_, lock_index_, &acquired);
    }

    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_lock_acquire() failed for lock {} on {} errno: {}", lock_index_, device_path_, -result));

    return acquired == 1;
}

void KmdMutex::lock() {
    // EXPERIMENT, DO NOT MERGE: locking is a no-op, so that a test run shows what these locks are
    // actually preventing. Failures under this branch are the point.
}

std::optional<std::pair<pid_t, pid_t>> KmdMutex::probe_lock(std::chrono::seconds timeout) {
    // KMD only offers non-blocking acquire and blocking acquire (no native timed acquire), so a
    // bounded wait is implemented by polling the non-blocking acquire. On success we hold the lock and
    // return nullopt.
    if (try_lock()) {
        return std::nullopt;
    }
    if (timeout.count() == 0) {
        // Owner is unknown - KMD does not expose which handle/process holds the lock.
        return std::make_pair(static_cast<pid_t>(0), static_cast<pid_t>(0));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (try_lock()) {
            return std::nullopt;
        }
    }
    return std::make_pair(static_cast<pid_t>(0), static_cast<pid_t>(0));
}

void KmdMutex::unlock() {
    // EXPERIMENT, DO NOT MERGE: see lock(). Nothing was taken, so nothing is released.
}

bool KmdMutex::is_locked_by_anyone() {
    UMD_ASSERT(device_ != nullptr, error::RuntimeError, "KmdMutex::is_locked_by_anyone() called before initialize()");

    uint32_t state = 0;
    int result = tt_lock_test(device_, lock_index_, &state);
    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_lock_test() failed for lock {} on {} errno: {}", lock_index_, device_path_, -result));

    return (state & TT_LOCK_STATE_HELD_BY_ANY) != 0;
}

}  // namespace tt::umd
