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

#include "umd/device/utils/mutex_interface.hpp"

// tt-kmd-lib device handle. Forward declared so this public header does not require tt-kmd-lib's
// include path, which UMD links privately.
struct tt_device_t;

namespace tt::umd {

// KmdMutex is a cross-process lock backed by the Tenstorrent kernel-mode driver (KMD) resource locks,
// which it drives through tt-kmd-lib's tt_lock_* API. See device/utils/README.md for how the
// available locking backends compare.
//
// Properties:
//   - The lock is tied to the device itself, not to a host filesystem. Any process that can open
//     /dev/tenstorrent/<N> contends over the same lock, even across different containers or mount
//     namespaces, with no /dev/shm (or other filesystem) sharing required. This makes it the right
//     primitive for serializing whole workloads against one device.
//   - Every operation is an ioctl (a syscall); there is no userspace fast path. A contended lock()
//     polls the non-blocking acquire once a millisecond (the ioctl set has no efficient blocking
//     wait), so waiting for a held lock costs more than a single syscall.
//   - The lock is held by the open file descriptor, and KMD releases all of an fd's locks when the
//     fd is closed. Since the kernel closes every fd of a process when it dies, a crashed holder
//     cannot leak the lock - it is reclaimed automatically.
//   - Scope is limited to a single local device: there is no global lock that spans devices.
//   - Threads of one process contend for the lock like separate processes do, even when they share
//     one KmdMutex, because KMD arbitrates on a device wide bit rather than on the file descriptor:
//     an acquire fails if the lock is held, including by the handle asking for it. So a single
//     instance can be shared by every thread that needs the lock.
//   - Releasing is not owner checked. KMD tracks which handle holds a lock, not which thread, so a
//     thread can release a lock another thread of the same process took. Taking the lock through
//     std::lock_guard or std::unique_lock keeps that from happening.
//   - Nothing here is annotated for TSAN, unlike RobustMutex, so data that is guarded only by a
//     KmdMutex is reported as racing under a TSAN build.
//
// This class owns its own dedicated device handle (chardev fd), separate from any fd used to run a
// workload. That is deliberate: a device reset invalidates every fd that was open across it (further
// ioctls return ENODEV), but the lock itself survives the reset and stays held until its fd is
// closed. Keeping the lock on a dedicated handle lets a caller hold the lock, reset the device, then
// open fresh fds to run a workload. lock() transparently reopens its handle and retries if it
// observes that a reset happened while it was waiting.
//
// It meets the C++ Lockable requirement (lock()/try_lock()/unlock()), so it can be used with RAII
// helpers like std::lock_guard and std::unique_lock.
class KmdMutex : public MutexInterface {
public:
    // @param pci_device_num  N in /dev/tenstorrent/N (a UMD logical device id / PCIDevice number).
    // @param lock_index      KMD resource lock index in [0, TT_RESOURCE_LOCK_COUNT). See
    //                        tt_kmd_lib.h for which indices KMD suggests for which purpose.
    KmdMutex(int pci_device_num, uint8_t lock_index);
    ~KmdMutex() noexcept;

    // Opens the dedicated device handle used to hold the lock. Must be called before
    // lock()/try_lock(). Kept separate from the constructor so that failures during setup are still
    // cleaned up by the destructor. Calling it again once the handle is open does nothing.
    void initialize() override;

    // Move-only so it can live in STL containers. Copying would alias the owned handle.
    KmdMutex(KmdMutex&& other) noexcept;
    KmdMutex& operator=(KmdMutex&& other) noexcept;
    KmdMutex(const KmdMutex&) = delete;
    KmdMutex& operator=(const KmdMutex&) = delete;

    // Blocks until the lock is acquired. If a reset is detected while waiting (the handle becomes
    // unusable), the handle is transparently reopened and the wait is retried.
    void lock() override;

    // Attempts to acquire without blocking. Returns true if acquired, false if held by another
    // handle.
    bool try_lock();

    // Tries to acquire the lock (immediately if timeout is zero, otherwise polling until timeout).
    // Returns std::nullopt if the lock was acquired (the caller now holds it). On contention returns a
    // {pid, tid} pair, but KMD does not expose the owner, so it is always {0, 0} here - the pair's
    // presence signals "held by someone else", nothing more.
    std::optional<std::pair<pid_t, pid_t>> probe_lock(std::chrono::seconds timeout = std::chrono::seconds(0)) override;

    // Releases the lock. It is also released automatically when this object (and thus its handle) is
    // destroyed, or when the owning process exits.
    void unlock() override;

    // Best-effort query of whether the lock is currently held by any handle (including this one).
    // This is inherently racy and intended for debugging/diagnostics only.
    bool is_locked_by_anyone();

private:
    void open_device();
    void close_device() noexcept;

    // Closes and reopens the handle after a reset made it unusable. Closing is what releases any lock
    // KMD still has recorded against the old handle. Returns whether the handle is usable again.
    bool reopen_device() noexcept;

    int pci_device_num_;
    uint8_t lock_index_;
    tt_device_t* device_ = nullptr;
    std::string device_path_;
};

}  // namespace tt::umd
