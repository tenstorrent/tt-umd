// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <utility>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/robust_mutex.hpp"
#ifdef TT_UMD_BUILD_SIMULATION
#include "umd/device/simulation/tt_sim_communicator.hpp"
#endif

namespace tt::umd {

using RuntimeException = error::UmdException<error::RuntimeError>;

TEST(MacOSPlatform, HardwareAccessIsUnsupported) {
    tt_device_t* device = nullptr;
    EXPECT_EQ(tt_device_open("/dev/tenstorrent/0", &device, 0), -ENOTSUP);
    EXPECT_EQ(device, nullptr);

    uint32_t value = 0x12345678;
    EXPECT_EQ(tt_noc_read32(nullptr, 1, 2, 0x10000, &value), -ENOTSUP);
    EXPECT_EQ(value, 0x12345678);
}

TEST(MacOSPlatform, RobustLocksCannotSilentlyBecomeProcessLocal) {
    RobustMutex lock("macos-platform-test");
    EXPECT_THROW(lock.initialize(), RuntimeException);
    EXPECT_THROW(lock.lock(), RuntimeException);
    EXPECT_THROW(lock.unlock(), RuntimeException);
    EXPECT_THROW(lock.probe_lock(std::chrono::seconds(0)), RuntimeException);

    RobustMutex moved(std::move(lock));
    EXPECT_THROW(moved.initialize(), RuntimeException);
    RobustMutex assigned("macos-platform-test-assigned");
    assigned = std::move(moved);
    EXPECT_THROW(assigned.initialize(), RuntimeException);
}

#ifdef TT_UMD_BUILD_SIMULATION
TEST(MacOSPlatform, LegacyIsolatedSimulatorCopyReportsUnsupported) {
    // No library is needed: the unsupported copy mode must be rejected before
    // opening a source file or issuing Linux memfd operations.
    TTSimCommunicator communicator("unused-simulator.so", true);
    EXPECT_THAT(
        [&] { communicator.initialize(); },
        ::testing::ThrowsMessage<RuntimeException>(::testing::HasSubstr("require Linux memfd")));
}
#endif

}  // namespace tt::umd
