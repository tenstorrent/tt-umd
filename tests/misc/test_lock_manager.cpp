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

// A chip specific lock on a PCIe device is held in KMD, and only there. Nothing takes the shared memory lock of the
// same name any more, which is what makes it safe to remove.
TEST(TestLockManager, ChipSpecificPcieLockIsHeldInKmd) {
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
        EXPECT_FALSE(shm_lock.probe_lock(std::chrono::seconds(0)).has_value())
            << "Shared memory lock should no longer be taken";
        shm_lock.unlock();
    }

    EXPECT_FALSE(kmd_lock.is_locked_by_anyone()) << "KMD resource lock should have been released";
}
