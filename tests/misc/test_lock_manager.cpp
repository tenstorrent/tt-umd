// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <string>
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

// A device that presents a PCIe surface with no /dev/tenstorrent node behind it opts out of the KMD half of the
// lock. Two things have to hold for that to be safe, and neither needs hardware to check: setting the lock up must
// not reach for a KMD node, and the shared memory half must keep the very name the KMD-backed path registers under,
// so that a caller which did take the KMD half still contends with this one.
TEST(TestLockManager, ChipSpecificPcieLockCanOptOutOfKmd) {
    // A device number no hardware has, for two reasons. The registry keeps whichever lock was registered under a name
    // first, so sharing a number with the test above would hand this one that test's composite lock instead. And it
    // is what makes the opt-out observable: taking the KMD half here would have to open /dev/tenstorrent/250.
    constexpr int device_num = 250;

    RobustMutex shm_lock(mem_barrier_lock_name(device_num));
    shm_lock.initialize();

    ASSERT_NO_THROW(LockManager::initialize_mutex(
        MutexType::MEM_BARRIER, device_num, IODeviceType::PCIe, /* kmd_lock_available= */ false))
        << "Opting out should not open a KMD lock device";

    {
        auto lock = LockManager::acquire_mutex(MutexType::MEM_BARRIER, device_num, IODeviceType::PCIe);

        EXPECT_TRUE(shm_lock.probe_lock(std::chrono::seconds(0)).has_value())
            << "Shared memory lock should be held, under the name the KMD-backed path registers under";
    }

    EXPECT_FALSE(shm_lock.probe_lock(std::chrono::seconds(0)).has_value())
        << "Shared memory lock should have been released";
    shm_lock.unlock();
}
