// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/mman.h>      // shm_unlink
#include <sys/resource.h>  // setrlimit
#include <sys/wait.h>      // waitpid
#include <unistd.h>        // getpid, fork, _exit

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "umd/device/utils/robust_mutex.hpp"

using namespace tt::umd;
using namespace std::chrono_literals;

// These tests exercise the locking behavior behind LockManager / RobustMutex, which is what UMD's
// non-MMIO read/write paths use to serialize ethernet command-queue access. They are hardware-free:
// the mutex lives in a /dev/shm file and never touches a device.
//
// Background (tt-telemetry hang investigation):
// A telemetry server kept hanging on 'NON_MMIO_0_PCIe' after a device was reset, and the lock stayed
// LOCKED long after the read had failed. The natural suspicion was "tt-telemetry swallows UMD's
// exception, so the lock is never released". These tests pin down what actually happens:
//   1. LockReleasedWhenHolderThrows  -> an exception thrown while the lock is held DOES release it,
//      because LockManager hands out a std::unique_lock<RobustMutex> whose destructor runs during
//      stack unwinding. So swallowing the exception upstream is NOT what leaks the lock.
//   2. LockStaysHeldWhenHolderNeverReturns -> if a code path holds the lock and neither returns nor
//      throws (e.g. an unbounded poll loop with no check_timeout), the lock is held forever and every
//      other waiter blocks indefinitely. This is the real shape of the production deadlock.

namespace {

// Unique, test-only mutex names so we never collide with real device locks in /dev/shm.
constexpr std::string_view kExceptionMutexName = "UMD_TEST_ROBUST_MUTEX_EXCEPTION";
constexpr std::string_view kHangMutexName = "UMD_TEST_ROBUST_MUTEX_HANG";
constexpr std::string_view kOrphanMutexName = "UMD_TEST_ROBUST_MUTEX_ORPHAN";
constexpr std::string_view kCrashMutexName = "UMD_TEST_ROBUST_MUTEX_CRASH";

// Remove any stale backing file so each crash test starts from a clean, freshly-initialized mutex
// (otherwise a previous run's orphaned lock would poison the next run).
void unlink_backing_file(std::string_view mutex_name) {
    std::string shm_name = std::string(RobustMutex::SHM_FILE_PREFIX) + std::string(mutex_name);
    shm_unlink(shm_name.c_str());
}

}  // namespace

// An exception thrown while a std::unique_lock<RobustMutex> is in scope releases the lock during
// stack unwinding (the unique_lock destructor calls unlock()). This is exactly the type LockManager
// returns, so the production read/write paths get this behavior for free. A subsequent acquirer must
// therefore succeed immediately.
TEST(RobustMutex, LockReleasedWhenHolderThrows) {
    RobustMutex mutex(kExceptionMutexName);
    mutex.initialize();

    try {
        // Same RAII wrapper LockManager::acquire_mutex() returns to callers like read_non_mmio().
        std::unique_lock<RobustMutex> lock(mutex);

        // Simulate read_non_mmio() hitting utils::check_timeout() and throwing while holding the lock.
        throw std::runtime_error("Simulated: Timeout waiting for Ethernet core service remote IO request.");
    } catch (const std::runtime_error&) {
        // tt-telemetry's caching_arc_telemetry_reader swallows this exact exception. By the time we
        // get here, the unique_lock destructor has already run and the lock is released.
    }

    // A non-blocking probe must acquire the lock, proving it is free (probe_lock returns nullopt on
    // success and leaves us holding it).
    std::optional<std::pair<pid_t, pid_t>> owner = mutex.probe_lock(0s);
    EXPECT_FALSE(owner.has_value()) << "Lock was still held after the holder threw; RAII did not release it.";
    if (!owner.has_value()) {
        mutex.unlock();
    }
}

// If a thread acquires the lock and then neither returns nor throws (modeling an unbounded poll loop
// that lacks a check_timeout), the lock is never released and any other participant blocks forever.
// This is the production failure: a stalled remote read holds NON_MMIO and starves every other
// process/thread that needs it, which is what the watchdog reports as a deadlock.
TEST(RobustMutex, LockStaysHeldWhenHolderNeverReturns) {
    RobustMutex mutex(kHangMutexName);
    mutex.initialize();

    std::atomic<bool> holder_has_lock{false};
    std::atomic<bool> allow_holder_to_release{false};

    std::thread holder([&]() {
        std::unique_lock<RobustMutex> lock(mutex);
        holder_has_lock = true;
        // Spin instead of returning/throwing: this is the timeout-less loop that holds the lock.
        while (!allow_holder_to_release.load()) {
            std::this_thread::sleep_for(1ms);
        }
        // unique_lock releases here once we're told to stop.
    });

    while (!holder_has_lock.load()) {
        std::this_thread::sleep_for(1ms);
    }

    // While the holder is stuck, a waiter cannot get the lock. probe_lock times out and reports the
    // current owner (this process, the holder thread) -- mirroring the "Waiting for lock ... held by
    // thread TID/PID" warning seen in the field.
    std::optional<std::pair<pid_t, pid_t>> owner = mutex.probe_lock(1s);
    ASSERT_TRUE(owner.has_value()) << "Expected the lock to be held by the stuck thread.";
    EXPECT_EQ(owner->first, getpid());

    // Let the holder finish; only now is the lock released.
    allow_holder_to_release = true;
    holder.join();

    std::optional<std::pair<pid_t, pid_t>> owner_after = mutex.probe_lock(0s);
    EXPECT_FALSE(owner_after.has_value()) << "Lock should be free once the holder returns.";
    if (!owner_after.has_value()) {
        mutex.unlock();
    }
}

// CONTROL: a process that crashes (abort) while holding the lock, WITHOUT destroying the RobustMutex
// first, is correctly recovered by the kernel's robust-futex mechanism. A fresh probe gets EOWNERDEAD,
// which RobustMutex::probe_lock() recovers, so it reports the lock as free.
//
// This is the baseline proving robust recovery normally survives a hard crash. It is paired with
// OrphanedWhenMutexDestroyedBeforeCrash below, which shows the one thing that breaks it.
TEST(RobustMutex, RecoveredWhenHolderCrashesWithRegionMapped) {
    unlink_backing_file(kCrashMutexName);

    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "fork failed";
    if (pid == 0) {
        // Child: acquire the lock and crash while still holding it. Do NOT destroy the RobustMutex,
        // so its /dev/shm mapping stays valid and the kernel's exit_robust_list walk succeeds.
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);  // keep the test run tidy: no core file
        RobustMutex mutex(kCrashMutexName);
        mutex.initialize();
        mutex.lock();
        std::abort();
        _exit(99);  // unreachable
    }

    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);

    RobustMutex probe(kCrashMutexName);
    probe.initialize();
    std::optional<std::pair<pid_t, pid_t>> owner = probe.probe_lock(0s);
    EXPECT_FALSE(owner.has_value())
        << "Robust recovery should have reclaimed the lock from the crashed owner (EOWNERDEAD).";
    if (!owner.has_value()) {
        probe.unlock();
    }
    unlink_backing_file(kCrashMutexName);
}

// Regression test for the close_mutex() destruction-while-held guard, which fixes the tt-telemetry
// orphaned-lock bug.
//
// The kernel only marks a robust mutex owner-dead if it can walk the dead thread's robust list, which
// lives in the mutex's shared-memory region. The original bug: RobustMutex::~RobustMutex ->
// close_mutex() munmap'd that region even while a thread still held the lock (e.g. tt-telemetry's
// watchdog exit(1) running static destructors while a stuck thread held NON_MMIO). With the region
// unmapped, the kernel's robust-list walk on the subsequent crash faulted and silently bailed, so
// OWNER_DIED was never set and the lock was orphaned forever (EBUSY for everyone, cross-process /
// cross-container).
//
// The fix: close_mutex() detects active_locks_ != 0 and leaks the mapping instead of unmapping it, so
// the robust list stays valid and the kernel can still set OWNER_DIED on crash. This test reproduces
// the exact teardown (destroy while held, then crash) and asserts the lock is now RECOVERABLE.
TEST(RobustMutex, DestroyedWhileHeldThenCrashStillRecovers) {
    unlink_backing_file(kOrphanMutexName);

    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "fork failed";
    if (pid == 0) {
        // Child: acquire the lock, then destroy the RobustMutex while still holding it (mimicking
        // exit()-time static destruction), then crash. close_mutex()'s guard must leak (not munmap)
        // the still-held region so robust recovery survives.
        struct rlimit no_core = {0, 0};
        setrlimit(RLIMIT_CORE, &no_core);
        {
            RobustMutex mutex(kOrphanMutexName);
            mutex.initialize();
            mutex.lock();
            // RobustMutex::lock() (unlike unique_lock) does NOT unlock on destruction, so when `mutex`
            // goes out of scope here its destructor runs close_mutex() while the lock is still held:
            // active_locks_ != 0 -> the guard leaks the mapping instead of munmap-ing it.
        }
        std::abort();
        _exit(99);  // unreachable
    }

    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);

    RobustMutex probe(kOrphanMutexName);
    probe.initialize();
    std::optional<std::pair<pid_t, pid_t>> owner = probe.probe_lock(0s);
    // With the guard, the region stayed mapped, so the kernel set OWNER_DIED on crash and probe_lock()
    // recovers it (EOWNERDEAD -> nullopt). Without the guard this would be orphaned (EBUSY -> owner set).
    EXPECT_FALSE(owner.has_value())
        << "Lock was orphaned: destroying a held RobustMutex munmap'd the region and broke robust "
           "recovery. The close_mutex() destruction-while-held guard should have leaked the mapping.";
    if (!owner.has_value()) {
        probe.unlock();
    }
    unlink_backing_file(kOrphanMutexName);
}
