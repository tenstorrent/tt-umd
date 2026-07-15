// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>  // pid_t

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace tt::umd {

// Conventional KMD resource-lock indices. These mirror the TENSTORRENT_LOCK_INDEX_* defines in KMD's
// ioctl.h: indices 0..15 are reserved by convention for ERISC cores. They are copied here (the KMD
// header lives in a separate repo and cannot be included/linked from UMD); keep this in sync with
// KMD. Callers coordinating their own resources should use indices outside this reserved range.
enum class KmdLockIndex : uint8_t {
    ETH00 = 0,
    ETH01 = 1,
    ETH02 = 2,
    ETH03 = 3,
    ETH04 = 4,
    ETH05 = 5,
    ETH06 = 6,
    ETH07 = 7,
    ETH08 = 8,
    ETH09 = 9,
    ETH10 = 10,
    ETH11 = 11,
    ETH12 = 12,
    ETH13 = 13,
    ETH14 = 14,
    ETH15 = 15,
};

// KmdMutex is a cross-process lock backed by the Tenstorrent kernel-mode driver (KMD) resource locks
// (TENSTORRENT_IOCTL_LOCK_CTL). See device/utils/README.md for how the available locking backends
// compare.
//
// Properties:
//   - The lock is tied to the device itself, not to a host filesystem. Any process that can open
//     /dev/tenstorrent/<N> contends over the same lock, even across different containers or mount
//     namespaces, with no /dev/shm (or other filesystem) sharing required. This makes it the right
//     primitive for serializing whole workloads against one device.
//   - Every operation is an ioctl (a syscall); there is no userspace fast path. A contended lock()
//     polls the non-blocking acquire with a short backoff (the ioctl set has no efficient blocking
//     wait), so waiting for a held lock costs more than a single syscall.
//   - The lock is held by the open file descriptor, and KMD releases all of an fd's locks when the
//     fd is closed. Since the kernel closes every fd of a process when it dies, a crashed holder
//     cannot leak the lock - it is reclaimed automatically.
//   - Scope is limited to a single local device: there is no global lock that spans devices.
//
// Each KMD device exposes TENSTORRENT_RESOURCE_LOCK_COUNT (64) independent lock indices (see
// KmdLockIndex for the reserved ones).
//
// This class owns its own dedicated chardev fd, separate from any fd used to run a workload. That
// is deliberate: a device reset invalidates every fd that was open across it (further ioctls return
// ENODEV), but the lock itself survives the reset and stays held until its fd is closed. Keeping the
// lock on a dedicated fd lets a caller hold the lock, reset the device, then open fresh fds to run a
// workload. lock() transparently reopens its fd and retries if it observes that a reset happened
// while it was waiting.
//
// It meets the C++ Lockable requirement (lock()/try_lock()/unlock()), so it can be used with RAII
// helpers like std::lock_guard and std::unique_lock.
class KmdMutex {
public:
    // @param pci_device_num  N in /dev/tenstorrent/N (a UMD logical device id / PCIDevice number).
    // @param lock_index      KMD resource lock index in [0, 64). Avoid 0..15 (reserved, see
    //                        KmdLockIndex).
    KmdMutex(int pci_device_num, uint8_t lock_index);
    ~KmdMutex() noexcept;

    // Opens the dedicated chardev fd used to hold the lock, after verifying the installed KMD supports
    // resource locks (throws otherwise). Must be called before lock()/try_lock(). Kept separate from
    // the constructor so that failures during setup are still cleaned up by the destructor.
    void initialize();

    // Move-only so it can live in STL containers. Copying would alias the owned fd.
    KmdMutex(KmdMutex&& other) noexcept;
    KmdMutex& operator=(KmdMutex&& other) noexcept;
    KmdMutex(const KmdMutex&) = delete;
    KmdMutex& operator=(const KmdMutex&) = delete;

    // Blocks until the lock is acquired. If a reset is detected while waiting (the fd becomes
    // unusable), the fd is transparently reopened and the wait is retried.
    void lock();

    // Attempts to acquire without blocking. Returns true if acquired, false if held by another fd.
    bool try_lock();

    // Tries to acquire the lock (immediately if timeout is zero, otherwise polling until timeout).
    // Returns std::nullopt if the lock was acquired (the caller now holds it). On contention returns a
    // {pid, tid} pair, but KMD does not expose the owner, so it is always {0, 0} here - the pair's
    // presence signals "held by someone else", nothing more.
    std::optional<std::pair<pid_t, pid_t>> probe_lock(std::chrono::seconds timeout = std::chrono::seconds(0));

    // Releases the lock. It is also released automatically when this object (and thus its fd) is
    // destroyed, or when the owning process exits.
    void unlock();

    // Best-effort query of whether the lock is currently held by any fd (including this one). This
    // is inherently racy and intended for debugging/diagnostics only.
    bool is_locked_by_anyone();

private:
    void open_device_fd();
    void close_fd() noexcept;

    int pci_device_num_;
    uint8_t lock_index_;
    int fd_ = -1;
    std::string device_path_;
};

}  // namespace tt::umd
