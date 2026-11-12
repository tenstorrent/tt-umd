// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>

#include "tt-kmd-lib/tt_kmd_lib.h"

// /dev/null gives a valid handle that is not a Tenstorrent device, so these cases run without a card.
// Each one is either rejected before reaching the driver or shows what a driver without
// TENSTORRENT_IOCTL_READ_EVENTS looks like to the caller.

namespace {

class KmdLibEvents : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_EQ(tt_device_open("/dev/null", &dev, 0), 0); }

    void TearDown() override { tt_device_close(dev); }

    tt_device_t* dev = nullptr;
    tt_event_t events[4] = {};
    uint32_t count = 0;
};

}  // namespace

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
