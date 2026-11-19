// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "tt-kmd-lib/tt_kmd_lib.h"

// /dev/null gives a valid handle that is not a Tenstorrent device, so these cases run without a card.
// Each one is rejected before reaching the driver, shows what a driver without
// TENSTORRENT_IOCTL_READ_EVENTS looks like to the caller, or relies on /dev/null always being readable.

// Not in an anonymous namespace: in a unity build, GCC rejects a TEST_F class deriving from one.
class KmdLibEvents : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_EQ(tt_device_open("/dev/null", &dev, 0), 0); }

    void TearDown() override { tt_device_close(dev); }

    tt_device_t* dev = nullptr;
    tt_event_t events[4] = {};
    uint32_t count = 0;
};

TEST_F(KmdLibEvents, NullDeviceIsRejected) {
    EXPECT_EQ(tt_device_read_events(nullptr, events, 4, 0, 0, -1, &count, nullptr, nullptr), -EINVAL);
}

TEST_F(KmdLibEvents, CountOutputIsRequired) {
    EXPECT_EQ(tt_device_read_events(dev, events, 4, 0, 0, -1, nullptr, nullptr, nullptr), -EINVAL);
}

TEST_F(KmdLibEvents, NullBufferIsRejectedWhenCapacityIsNonzero) {
    EXPECT_EQ(tt_device_read_events(dev, nullptr, 4, 0, 0, -1, &count, nullptr, nullptr), -EINVAL);
}

TEST_F(KmdLibEvents, UnknownFlagsAreRejected) {
    EXPECT_EQ(
        tt_device_read_events(dev, events, 4, ~TT_READ_EVENTS_FLAG_FROM_OLDEST, 0, -1, &count, nullptr, nullptr),
        -EINVAL);
}

TEST_F(KmdLibEvents, HandleWithoutReadEventsSupportReportsTheIoctlError) {
    EXPECT_EQ(tt_device_read_events(dev, events, 4, 0, 0, -1, &count, nullptr, nullptr), -ENOTTY);
}

TEST_F(KmdLibEvents, WaitRejectsNullDeviceArray) {
    uint8_t ready[1] = {};
    EXPECT_EQ(tt_device_wait_events(nullptr, 1, 0, -1, ready, &count), -EINVAL);
}

TEST_F(KmdLibEvents, WaitRejectsNullDeviceEntry) {
    tt_device_t* devices[2] = {dev, nullptr};
    uint8_t ready[2] = {};
    EXPECT_EQ(tt_device_wait_events(devices, 2, 0, -1, ready, &count), -EINVAL);
}

TEST_F(KmdLibEvents, WaitRejectsNullReadyArray) {
    EXPECT_EQ(tt_device_wait_events(&dev, 1, 0, -1, nullptr, &count), -EINVAL);
}

TEST_F(KmdLibEvents, WaitRequiresReadyCountOutput) {
    uint8_t ready[1] = {};
    EXPECT_EQ(tt_device_wait_events(&dev, 1, 0, -1, ready, nullptr), -EINVAL);
}

TEST_F(KmdLibEvents, WaitWithoutDevicesTimesOut) {
    count = 1;
    EXPECT_EQ(tt_device_wait_events(nullptr, 0, 0, -1, nullptr, &count), 0);
    EXPECT_EQ(count, 0u);
}

// /dev/null is always readable, which stands in for a device with unread records.
TEST_F(KmdLibEvents, WaitReportsReadableHandles) {
    tt_device_t* other = nullptr;
    ASSERT_EQ(tt_device_open("/dev/null", &other, 0), 0);
    tt_device_t* devices[2] = {dev, other};
    uint8_t ready[2] = {};

    EXPECT_EQ(tt_device_wait_events(devices, 2, 0, -1, ready, &count), 0);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(ready[0], 1u);
    EXPECT_EQ(ready[1], 1u);

    tt_device_close(other);
}

TEST_F(KmdLibEvents, WaitEndsWhenWakeFdIsReadable) {
    const int wake_fd = eventfd(1, EFD_CLOEXEC);
    ASSERT_GE(wake_fd, 0);

    // Bounded, so that a wake_fd that is not polled fails the test instead of hanging it.
    EXPECT_EQ(tt_device_wait_events(nullptr, 0, 10000, wake_fd, nullptr, &count), -ECANCELED);
    EXPECT_EQ(count, 0u);

    close(wake_fd);
}
