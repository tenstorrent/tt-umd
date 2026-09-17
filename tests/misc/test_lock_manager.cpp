// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/kmd_mutex.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/robust_mutex.hpp"

using namespace tt::umd;

namespace {

// The lock name and the KMD lock index are what different processes agree on, so they are spelled out here rather than
// read back from LockManager: a change to either breaks serialization against processes built from another UMD
// version, and this test is what should notice.
constexpr uint8_t MEM_BARRIER_KMD_LOCK_INDEX = 19;

std::string mem_barrier_lock_name(int device_num) { return "MEM_BARRIER_" + std::to_string(device_num) + "_PCIe"; }

}  // namespace

// A chip specific lock on a PCIe device has to be held in both places at once: in KMD, so that processes sharing only
// the device serialize, and in shared memory, so that processes on an older UMD - which know only about that one -
// still serialize too.
TEST(TestLockManager, ChipSpecificPcieLockIsHeldInBothBackends) {
    std::vector<int> devices = PCIDevice::enumerate_devices();
    if (devices.empty()) {
        GTEST_SKIP() << "No /dev/tenstorrent device present";
    }
    const int device_num = devices.front();

    KmdMutex kmd_lock(device_num, MEM_BARRIER_KMD_LOCK_INDEX);
    kmd_lock.initialize();
    RobustMutex shm_lock(mem_barrier_lock_name(device_num));
    shm_lock.initialize();

    LockManager::initialize_mutex(MutexType::MEM_BARRIER, device_num, IODeviceType::PCIe);

    {
        auto lock = LockManager::acquire_mutex(MutexType::MEM_BARRIER, device_num, IODeviceType::PCIe);

        EXPECT_TRUE(kmd_lock.is_locked_by_anyone()) << "KMD resource lock should be held";
        EXPECT_TRUE(shm_lock.probe_lock(std::chrono::seconds(0)).has_value()) << "Shared memory lock should be held";
    }

    EXPECT_FALSE(kmd_lock.is_locked_by_anyone()) << "KMD resource lock should have been released";
    EXPECT_FALSE(shm_lock.probe_lock(std::chrono::seconds(0)).has_value())
        << "Shared memory lock should have been released";
    shm_lock.unlock();
}

// try_acquire_mutex() takes the lock when it is free and hands back a lock owning it, so that a
// caller can tell the two apart with owns_lock() and does not have to wait to find out. A lock on a
// JTAG device is backed by shared memory alone, so this needs no hardware.
TEST(TestLockManager, TryAcquireTakesAFreeLockAndReportsAHeldOne) {
    // A device id no test hardware uses, so a stale lock file cannot make this test look wrong.
    constexpr int UNUSED_DEVICE_ID = 4242;
    LockManager::initialize_mutex(MutexType::MEM_BARRIER, UNUSED_DEVICE_ID, IODeviceType::JTAG);

    {
        auto lock = LockManager::try_acquire_mutex(MutexType::MEM_BARRIER, UNUSED_DEVICE_ID, IODeviceType::JTAG);
        ASSERT_TRUE(lock.owns_lock()) << "A free lock should have been acquired";

        // The lock is taken, so probing it from here reports an owner rather than acquiring it.
        RobustMutex same_lock("MEM_BARRIER_" + std::to_string(UNUSED_DEVICE_ID) + "_JTAG");
        same_lock.initialize();
        EXPECT_TRUE(same_lock.probe_lock(std::chrono::seconds(0)).has_value()) << "Lock should be held";
    }

    // Leaving the scope released it, so it can be taken again. Anything short of adopting what
    // probe_lock() acquired - locking a second time, or not owning it at all - shows up here.
    auto relock = LockManager::try_acquire_mutex(MutexType::MEM_BARRIER, UNUSED_DEVICE_ID, IODeviceType::JTAG);
    EXPECT_TRUE(relock.owns_lock()) << "The lock should have been released when it went out of scope";
}

// A lock already held is reported as busy rather than waited for.
TEST(TestLockManager, TryAcquireDoesNotWaitForAHeldLock) {
    constexpr int UNUSED_DEVICE_ID = 4243;
    LockManager::initialize_mutex(MutexType::MEM_BARRIER, UNUSED_DEVICE_ID, IODeviceType::JTAG);

    auto held = LockManager::acquire_mutex(MutexType::MEM_BARRIER, UNUSED_DEVICE_ID, IODeviceType::JTAG);
    ASSERT_TRUE(held.owns_lock());

    // Taken from another thread: a robust pthread mutex reports the owning thread re-taking it as an
    // error rather than as contention, so asking from this one would not be the same question.
    std::thread contender([] {
        const auto start = std::chrono::steady_clock::now();
        auto lock = LockManager::try_acquire_mutex(MutexType::MEM_BARRIER, UNUSED_DEVICE_ID, IODeviceType::JTAG);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        EXPECT_FALSE(lock.owns_lock()) << "A held lock should not have been acquired";
        EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100)
            << "try_acquire_mutex() should return without waiting for the lock";
    });
    contender.join();
}
