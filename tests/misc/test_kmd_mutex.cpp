// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/kmd_mutex.hpp"

using namespace tt::umd;

// A KMD resource lock is held by the file descriptor that took it, which makes it fair to ask whether
// threads of one process exclude each other at all. They do, and these tests pin that down, because
// UMD hands one mutex object to every thread that needs a given lock. What arbitrates is a device wide
// bit, not the handle: an acquire fails while the lock is held, no matter who asks.

namespace {

// Test-only index, kept away from the ones UMD takes on a real device.
constexpr uint8_t TEST_KMD_LOCK_INDEX = 33;

std::optional<int> first_device() {
    std::vector<int> devices = PCIDevice::enumerate_devices();
    if (devices.empty()) {
        return std::nullopt;
    }
    return devices.front();
}

}  // namespace

// Taking the lock twice from the same handle must fail the second time. This is what makes threads of
// one process exclude each other, since they share the handle.
TEST(TestKmdMutex, SameHandleCannotAcquireTwice) {
    std::optional<int> device_num = first_device();
    if (!device_num.has_value()) {
        GTEST_SKIP() << "No /dev/tenstorrent device present";
    }

    KmdMutex mutex(*device_num, TEST_KMD_LOCK_INDEX);
    mutex.initialize();

    ASSERT_TRUE(mutex.try_lock());
    EXPECT_FALSE(mutex.try_lock()) << "The handle holding the lock acquired it a second time";
    mutex.unlock();
}

// A second handle over the same lock is what two processes look like to KMD.
TEST(TestKmdMutex, SecondHandleCannotAcquireWhileHeld) {
    std::optional<int> device_num = first_device();
    if (!device_num.has_value()) {
        GTEST_SKIP() << "No /dev/tenstorrent device present";
    }

    KmdMutex holder(*device_num, TEST_KMD_LOCK_INDEX);
    holder.initialize();
    KmdMutex contender(*device_num, TEST_KMD_LOCK_INDEX);
    contender.initialize();

    {
        std::lock_guard<KmdMutex> lock(holder);
        EXPECT_FALSE(contender.try_lock()) << "A second handle acquired a lock that was already held";
        EXPECT_TRUE(contender.is_locked_by_anyone());
    }

    EXPECT_TRUE(contender.try_lock()) << "Lock was not released";
    contender.unlock();
}

// Same again, with the second handle opened and used from another thread, since that is how UMD meets
// this lock: threads that never share a mutex object still have to serialize.
TEST(TestKmdMutex, HandleInAnotherThreadCannotAcquireWhileHeld) {
    std::optional<int> device_num = first_device();
    if (!device_num.has_value()) {
        GTEST_SKIP() << "No /dev/tenstorrent device present";
    }

    KmdMutex holder(*device_num, TEST_KMD_LOCK_INDEX);
    holder.initialize();

    // Opening the handle in the thread as well, so nothing about it is shared with the holder.
    auto try_acquire_in_thread = [&]() {
        bool acquired = false;
        std::thread thread([&]() {
            KmdMutex contender(*device_num, TEST_KMD_LOCK_INDEX);
            contender.initialize();
            acquired = contender.try_lock();
            if (acquired) {
                contender.unlock();
            }
        });
        thread.join();
        return acquired;
    };

    {
        std::lock_guard<KmdMutex> lock(holder);
        EXPECT_FALSE(try_acquire_in_thread()) << "Another thread acquired a lock this one was holding";
    }

    EXPECT_TRUE(try_acquire_in_thread()) << "Lock was not released";
}
