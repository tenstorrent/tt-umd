// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>

#include "umd/device/coordinates/att/att_window.hpp"

using namespace tt::umd;

// A real window shape to exercise the address math against, kept independent of the product maps
// so that changing one does not move these expectations.
constexpr att::Window NEO_L1_WINDOW{
    .compare = 0x10000000000ULL,
    .mask_bits = 30,
    .endpoint_shift = 24,
    .endpoint_size = 6,
    .endpoint_table_offset = 128,
    .translate_address = true,
};

TEST(AttWindow, MakeAddressComposesBaseSelectorAndOffset) {
    // Selector 0 at offset 0 is the window base itself.
    EXPECT_EQ(NEO_L1_WINDOW.make_address(0, 0), 0x10000000000ULL);

    // The selector occupies bits [29:24], so each step is one 16 MiB tile.
    EXPECT_EQ(NEO_L1_WINDOW.make_address(1, 0), 0x10001000000ULL);
    EXPECT_EQ(NEO_L1_WINDOW.make_address(31, 0), 0x1001f000000ULL);

    // The local offset passes through the low bits untouched.
    EXPECT_EQ(NEO_L1_WINDOW.make_address(0, 0x100000), 0x10000100000ULL);
}

TEST(AttWindow, AddressDecodesBackToSelectorAndOffset) {
    const uint64_t address = NEO_L1_WINDOW.make_address(17, 0x2000);

    EXPECT_EQ(NEO_L1_WINDOW.selector(address), 17u);
    EXPECT_EQ(NEO_L1_WINDOW.endpoint_index(address), 128 + 17);
    EXPECT_EQ(NEO_L1_WINDOW.local_address(address), 0x2000ULL);
}

TEST(AttWindow, MatchesOnlyAddressesInsideTheWindow) {
    EXPECT_TRUE(NEO_L1_WINDOW.matches(NEO_L1_WINDOW.make_address(0, 0)));
    EXPECT_TRUE(NEO_L1_WINDOW.matches(NEO_L1_WINDOW.make_address(63, 0xffffff)));

    // The per-tile config window sits below this one and must not be claimed by it.
    EXPECT_FALSE(NEO_L1_WINDOW.matches(0x1800000000ULL));
    EXPECT_FALSE(NEO_L1_WINDOW.matches(0x1000000000000ULL));
}

TEST(AttWindow, RejectsTransfersThatSpillIntoTheNextSlot) {
    // The NEO L1 window addresses one tile per 1 << 24 bytes.
    EXPECT_TRUE(NEO_L1_WINDOW.transfer_supported(0x1000, 4));
    EXPECT_TRUE(NEO_L1_WINDOW.transfer_supported(0xfffffc, 4));

    // Spilling past the granule silently lands on the next tile, so it is refused.
    EXPECT_FALSE(NEO_L1_WINDOW.transfer_supported(0xfffffc, 8));
    EXPECT_FALSE(NEO_L1_WINDOW.transfer_supported(0x1000000, 1));
    EXPECT_FALSE(NEO_L1_WINDOW.transfer_supported(0x1000, 0));
}

TEST(AttWindow, RejectsSelectorsWiderThanTheField) {
    EXPECT_TRUE(NEO_L1_WINDOW.selector_supported(63));
    EXPECT_FALSE(NEO_L1_WINDOW.selector_supported(64));
}
