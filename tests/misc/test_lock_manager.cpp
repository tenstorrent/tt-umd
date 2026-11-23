// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "tt-umd-utils/robust_mutex.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/kmd_mutex.hpp"
#include "tt-umd/utils/lock_manager.hpp"

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

// A device the caller declares private to this process has only this process's threads to lock out. Its locks must
// still exclude those threads, but must leave no file in /dev/shm: such a device is named after this process, so a file
// would outlive it with nothing left to use or remove it.
struct PrivateDeviceLockCase {
    std::string name;
    IODeviceType device_type;
    bool shared_across_processes;
    // Device ids no hardware has, distinct per case and per test: the registry keeps whichever lock was registered
    // under a name first, so sharing one would hand a test another test's lock. For PCIe it also makes taking a KMD
    // lock observable, since that would have to open /dev/tenstorrent/<id>.
    int no_file_device_id;
    int exclusion_device_id;
    int probe_device_id;
    // The file a shared memory lock on no_file_device_id would be backed by.
    std::string shm_file;
};

class TestLockManagerPrivateDevice : public ::testing::TestWithParam<PrivateDeviceLockCase> {};

TEST_P(TestLockManagerPrivateDevice, LockLeavesNoSharedMemoryFile) {
    const auto& param = GetParam();
    const std::string& shm_file = param.shm_file;
    // Clears a file an earlier failing run may have left. The error is ignored: in sticky /dev/shm a file another user
    // left cannot be removed, and then the check below rightly fails.
    std::error_code ignored;
    std::filesystem::remove(shm_file, ignored);

    ASSERT_NO_THROW(LockManager::initialize_mutex(
        MutexType::MEM_BARRIER, param.no_file_device_id, param.device_type, param.shared_across_processes))
        << "A private lock should not open a KMD lock device";
    {
        auto lock = LockManager::acquire_mutex(MutexType::MEM_BARRIER, param.no_file_device_id, param.device_type);
        ASSERT_TRUE(lock.owns_lock());
    }

    EXPECT_FALSE(std::filesystem::exists(shm_file)) << shm_file << " should not have been created";
}

TEST_P(TestLockManagerPrivateDevice, LockExcludesAnotherThread) {
    const auto& param = GetParam();
    LockManager::initialize_mutex(
        MutexType::MEM_BARRIER, param.exclusion_device_id, param.device_type, param.shared_across_processes);

    {
        auto held = LockManager::acquire_mutex(MutexType::MEM_BARRIER, param.exclusion_device_id, param.device_type);
        ASSERT_TRUE(held.owns_lock());
        const pid_t holder_tid = static_cast<pid_t>(syscall(SYS_gettid));

        std::thread contender([&param, holder_tid] {
            auto lock =
                LockManager::try_acquire_mutex(MutexType::MEM_BARRIER, param.exclusion_device_id, param.device_type);
            EXPECT_FALSE(lock.owns_lock()) << "A lock held by another thread should not have been acquired";

            const auto owner =
                LockManager::probe_mutex(MutexType::MEM_BARRIER, param.exclusion_device_id, param.device_type);
            ASSERT_TRUE(owner.has_value()) << "probe_mutex() should report the lock as held";
            EXPECT_EQ(owner->first, getpid()) << "The lock should be reported as held by this process";
            EXPECT_EQ(owner->second, holder_tid) << "The lock should be reported as held by the thread that took it";
        });
        contender.join();
    }

    auto relock = LockManager::try_acquire_mutex(MutexType::MEM_BARRIER, param.exclusion_device_id, param.device_type);
    EXPECT_TRUE(relock.owns_lock()) << "The lock should have been released when it went out of scope";
}

TEST_P(TestLockManagerPrivateDevice, ProbeReportsAFreeLockAsFree) {
    const auto& param = GetParam();
    LockManager::initialize_mutex(
        MutexType::MEM_BARRIER, param.probe_device_id, param.device_type, param.shared_across_processes);

    EXPECT_FALSE(LockManager::probe_mutex(MutexType::MEM_BARRIER, param.probe_device_id, param.device_type).has_value())
        << "A lock nobody holds should be reported as free";

    // Probing must not leave the lock taken behind it.
    auto lock = LockManager::try_acquire_mutex(MutexType::MEM_BARRIER, param.probe_device_id, param.device_type);
    EXPECT_TRUE(lock.owns_lock()) << "A probed free lock should still be free to take";
}

INSTANTIATE_TEST_SUITE_P(
    TestLockManager,
    TestLockManagerPrivateDevice,
    ::testing::Values(
        // A simulated device presents a PCIe surface, so only the caller can say it is private.
        PrivateDeviceLockCase{
            "PcieDeclaredPrivate",
            IODeviceType::PCIe,
            false,
            270,
            280,
            290,
            "/dev/shm/TT_UMD_LOCK.MEM_BARRIER_270_PCIe"},
        PrivateDeviceLockCase{
            "JtagDeclaredPrivate",
            IODeviceType::JTAG,
            false,
            271,
            281,
            291,
            "/dev/shm/TT_UMD_LOCK.MEM_BARRIER_271_JTAG"},
        PrivateDeviceLockCase{
            "UndefinedDeclaredPrivate",
            IODeviceType::UNDEFINED,
            false,
            272,
            282,
            292,
            "/dev/shm/TT_UMD_LOCK.MEM_BARRIER_272_Undefined"}),
    [](const ::testing::TestParamInfo<PrivateDeviceLockCase>& info) { return info.param.name; });

// A private PCIe lock is backed by this process alone, so taking and probing it must work with no KMD device behind
// the id: a simulated device has none.
TEST(TestLockManager, PrivatePcieLockNeedsNoKmdDevice) {
    // No hardware has this id, so anything that opened its KMD node would throw.
    ASSERT_FALSE(std::filesystem::exists("/dev/tenstorrent/273"));
    ASSERT_NO_THROW(
        LockManager::initialize_mutex(MutexType::ARC_MSG, 273, IODeviceType::PCIe, /*shared_across_processes=*/false));

    {
        std::unique_lock<MutexInterface> lock;
        ASSERT_NO_THROW(lock = LockManager::acquire_mutex(MutexType::ARC_MSG, 273, IODeviceType::PCIe));
        EXPECT_TRUE(lock.owns_lock());
    }

    std::optional<std::pair<pid_t, pid_t>> owner;
    ASSERT_NO_THROW(owner = LockManager::probe_mutex(MutexType::ARC_MSG, 273, IODeviceType::PCIe));
    EXPECT_FALSE(owner.has_value()) << "The released lock should be reported as free";
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

    // Asked from another thread, since that is the contention a caller actually runs into.
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
