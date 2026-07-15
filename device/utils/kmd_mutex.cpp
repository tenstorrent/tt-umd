// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/kmd_mutex.hpp"

#include <fcntl.h>  // ::open, O_RDWR, O_CLOEXEC, O_APPEND
#include <fmt/format.h>
#include <sys/ioctl.h>  // ioctl
#include <unistd.h>     // ::close

#include <cerrno>
#include <chrono>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "pcie/ioctl.h"  // TENSTORRENT_IOCTL_LOCK_CTL and friends
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/kmd_versions.hpp"

namespace tt::umd {

KmdMutex::KmdMutex(int pci_device_num, uint8_t lock_index) :
    pci_device_num_(pci_device_num),
    lock_index_(lock_index),
    device_path_(fmt::format("/dev/tenstorrent/{}", pci_device_num)) {
    UMD_ASSERT(
        lock_index_ < TENSTORRENT_RESOURCE_LOCK_COUNT,
        error::RuntimeError,
        fmt::format("KMD resource lock index {} out of range [0, {})", lock_index_, TENSTORRENT_RESOURCE_LOCK_COUNT));
}

KmdMutex::~KmdMutex() noexcept { close_fd(); }

KmdMutex::KmdMutex(KmdMutex&& other) noexcept :
    pci_device_num_(other.pci_device_num_),
    lock_index_(other.lock_index_),
    fd_(other.fd_),
    device_path_(std::move(other.device_path_)) {
    // Invalidate the other object so its destructor doesn't close our fd.
    other.fd_ = -1;
}

KmdMutex& KmdMutex::operator=(KmdMutex&& other) noexcept {
    if (this != &other) {
        close_fd();  // clean up existing resources

        pci_device_num_ = other.pci_device_num_;
        lock_index_ = other.lock_index_;
        fd_ = other.fd_;
        device_path_ = std::move(other.device_path_);

        other.fd_ = -1;
    }
    return *this;
}

void KmdMutex::open_device_fd() {
    // O_APPEND tells KMD (>= 2.6) not to request high power on open; a lock-only fd should not change
    // the device's power state. The flag is harmless on older KMD versions. O_CLOEXEC avoids leaking
    // the lock fd into child processes, which would keep the lock held longer than intended.
    fd_ = ::open(device_path_.c_str(), O_RDWR | O_CLOEXEC | O_APPEND);
    UMD_ASSERT(
        fd_ != -1,
        error::RuntimeError,
        fmt::format("open() failed for KMD lock device {} errno: {}", device_path_, std::to_string(errno)));
}

void KmdMutex::initialize() {
    // Resource locks require a KMD new enough to implement the LOCK_CTL ioctl; on older KMD the
    // ioctl would simply fail at use time. Fail early and clearly instead.
    SemVer kmd_version = PCIDevice::read_kmd_version();
    UMD_ASSERT(
        kmd_version >= KMD_RESOURCE_LOCKS,
        error::RuntimeError,
        fmt::format(
            "KmdMutex requires KMD version {} or newer for resource locks, but the installed KMD is {}.",
            KMD_RESOURCE_LOCKS.to_string(),
            kmd_version.to_string()));

    open_device_fd();
}

void KmdMutex::close_fd() noexcept {
    if (fd_ != -1) {
        // Closing the fd releases any lock held on it (enforced by KMD), so this is also our unlock
        // path on destruction.
        if (::close(fd_) != 0) {
            // Destructor path: log instead of throwing.
            log_warning(
                tt::LogUMD, "close() failed for KMD lock device {} errno: {}", device_path_, std::to_string(errno));
        }
        fd_ = -1;
    }
}

bool KmdMutex::try_lock() {
    UMD_ASSERT(fd_ != -1, error::RuntimeError, "KmdMutex::try_lock() called before initialize()");

    tenstorrent_lock_ctl lock_ctl{};
    lock_ctl.in.output_size_bytes = sizeof(lock_ctl.out);
    lock_ctl.in.flags = TENSTORRENT_LOCK_CTL_ACQUIRE;
    lock_ctl.in.index = lock_index_;

    if (ioctl(fd_, TENSTORRENT_IOCTL_LOCK_CTL, &lock_ctl) != 0) {
        // A reset across our fd makes it unusable; reopen and retry once on a fresh fd.
        if (errno == ENODEV) {
            close_fd();
            open_device_fd();
            if (ioctl(fd_, TENSTORRENT_IOCTL_LOCK_CTL, &lock_ctl) == 0) {
                return lock_ctl.out.value == 1;
            }
        }
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "LOCK_CTL ACQUIRE failed for lock {} on {} errno: {}",
                lock_index_,
                device_path_,
                std::to_string(errno)));
    }

    // out.value: 1 = acquired, 0 = held by another fd.
    return lock_ctl.out.value == 1;
}

void KmdMutex::lock() {
    UMD_ASSERT(fd_ != -1, error::RuntimeError, "KmdMutex::lock() called before initialize()");

    // The LOCK_CTL ioctl set used here exposes only non-blocking acquire, so a blocking lock is
    // implemented by polling the non-blocking acquire with a short backoff. This also deliberately
    // avoids KMD's blocking-acquire path, which has had a deadlock against the reset ioctl. try_lock()
    // already handles a reset (ENODEV) by reopening the fd.
    while (!try_lock()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::optional<std::pair<pid_t, pid_t>> KmdMutex::probe_lock(std::chrono::seconds timeout) {
    // KMD only offers non-blocking acquire and blocking acquire (no native timed acquire), so a
    // bounded wait is implemented by polling the non-blocking acquire. On success we hold the lock and
    // return nullopt.
    if (try_lock()) {
        return std::nullopt;
    }
    if (timeout.count() == 0) {
        // Owner is unknown - KMD does not expose which fd/process holds the lock.
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
    UMD_ASSERT(fd_ != -1, error::RuntimeError, "KmdMutex::unlock() called before initialize()");

    tenstorrent_lock_ctl lock_ctl{};
    lock_ctl.in.output_size_bytes = sizeof(lock_ctl.out);
    lock_ctl.in.flags = TENSTORRENT_LOCK_CTL_RELEASE;
    lock_ctl.in.index = lock_index_;

    if (ioctl(fd_, TENSTORRENT_IOCTL_LOCK_CTL, &lock_ctl) != 0) {
        // If the fd died due to a reset, the lock is still held in KMD until the fd is closed; the
        // destructor's close() will release it, so this is not fatal.
        if (errno == ENODEV) {
            log_warning(
                tt::LogUMD,
                "LOCK_CTL RELEASE for lock {} on {} hit ENODEV (device reset); lock will be released on close",
                lock_index_,
                device_path_);
            return;
        }
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "LOCK_CTL RELEASE failed for lock {} on {} errno: {}",
                lock_index_,
                device_path_,
                std::to_string(errno)));
    }

    // out.value == 0 means we did not actually hold the lock. Benign (e.g. double unlock), but worth a
    // debug trace.
    if (lock_ctl.out.value == 0) {
        log_debug(tt::LogUMD, "LOCK_CTL RELEASE for lock {} on {}: lock was not held by us", lock_index_, device_path_);
    }
}

bool KmdMutex::is_locked_by_anyone() {
    UMD_ASSERT(fd_ != -1, error::RuntimeError, "KmdMutex::is_locked_by_anyone() called before initialize()");

    tenstorrent_lock_ctl lock_ctl{};
    lock_ctl.in.output_size_bytes = sizeof(lock_ctl.out);
    lock_ctl.in.flags = TENSTORRENT_LOCK_CTL_TEST;
    lock_ctl.in.index = lock_index_;

    UMD_ASSERT(
        ioctl(fd_, TENSTORRENT_IOCTL_LOCK_CTL, &lock_ctl) == 0,
        error::RuntimeError,
        fmt::format(
            "LOCK_CTL TEST failed for lock {} on {} errno: {}", lock_index_, device_path_, std::to_string(errno)));

    // out.value: bit 0 = held by us, bit 1 = held by any fd.
    return (lock_ctl.out.value & 0b10) != 0;
}

}  // namespace tt::umd
