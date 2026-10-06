// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>

#include "tt-kmd-lib/tt_kmd_lib.h"

// Quasar reaches a target through the kernel's scalar NOC ioctls rather than through a mapped TLB
// window, so tt_noc_read_scalar/tt_noc_write_scalar are the access primitives on that path. The kernel enforces
// the same rules on its side; rejecting a malformed request here means a caller gets a diagnosable
// error instead of an EINVAL from an ioctl it cannot see.
//
// These cases all fail before any device is touched, which is what lets them run without one.

namespace {

constexpr uint64_t QUASAR_ADDRESS_LIMIT = uint64_t{1} << 52;

}  // namespace

TEST(KmdLibNocIo, NullDeviceIsRejected) {
    uint64_t value = 0;

    EXPECT_EQ(tt_noc_read_scalar(nullptr, 0, &value, 4, 0), -EINVAL);
    EXPECT_EQ(tt_noc_write_scalar(nullptr, 0, 0, 4, 0), -EINVAL);
}

// The ioctl carries the value in a fixed 64-bit field and the driver writes only the low `width`
// bytes, so width is not free-form.
// Only the four scalar sizes are accepted; acceptance of a valid width cannot be checked without a
// device, so this pins the rejections.
TEST(KmdLibNocIo, WidthThatIsNotAScalarAccessSizeIsRejected) {
    uint64_t value = 0;

    for (uint32_t width : {0u, 3u, 5u, 6u, 7u, 16u}) {
        EXPECT_EQ(tt_noc_read_scalar(nullptr, 0, &value, width, 0), -EINVAL) << "width " << width;
        EXPECT_EQ(tt_noc_write_scalar(nullptr, 0, 0, width, 0), -EINVAL) << "width " << width;
    }
}

TEST(KmdLibNocIo, AddressMustBeNaturallyAligned) {
    uint64_t value = 0;

    EXPECT_EQ(tt_noc_read_scalar(nullptr, 0x1, &value, 2, 0), -EINVAL);
    EXPECT_EQ(tt_noc_read_scalar(nullptr, 0x2, &value, 4, 0), -EINVAL);
    EXPECT_EQ(tt_noc_read_scalar(nullptr, 0x4, &value, 8, 0), -EINVAL);
    EXPECT_EQ(tt_noc_write_scalar(nullptr, 0x1, 0, 4, 0), -EINVAL);
}

// The inbound translation table's target-address field is 52 bits wide, so an address that does not
// fit it cannot be reached at all.
TEST(KmdLibNocIo, AddressMustFitTheTargetAddressField) {
    uint64_t value = 0;

    EXPECT_EQ(tt_noc_read_scalar(nullptr, QUASAR_ADDRESS_LIMIT, &value, 4, 0), -EINVAL);
    EXPECT_EQ(tt_noc_read_scalar(nullptr, QUASAR_ADDRESS_LIMIT - 4, &value, 8, 0), -EINVAL);
    EXPECT_EQ(tt_noc_write_scalar(nullptr, QUASAR_ADDRESS_LIMIT, 0, 4, 0), -EINVAL);
}

TEST(KmdLibNocIo, UnknownFlagsAreRejected) {
    uint64_t value = 0;

    EXPECT_EQ(tt_noc_read_scalar(nullptr, 0, &value, 4, ~TT_NOC_FLAG_KLA), -EINVAL);
    EXPECT_EQ(tt_noc_write_scalar(nullptr, 0, 0, 4, ~TT_NOC_FLAG_KLA), -EINVAL);
}

TEST(KmdLibNocIo, ReadRequiresAnOutputPointer) { EXPECT_EQ(tt_noc_read_scalar(nullptr, 0, nullptr, 4, 0), -EINVAL); }

// A Quasar package presents one PCIe function whichever chiplets it is built from, so the library
// names the architecture rather than the combination.
TEST(KmdLibNocIo, QuasarArchIsDistinctFromUnknown) { EXPECT_NE(TT_DEVICE_ARCH_QUASAR, TT_DEVICE_ARCH_UNKNOWN); }
