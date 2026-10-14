// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>

#include "tt-umd/coordinates/att/att_window.hpp"

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
    .bar = 0,
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

// qsr.s1 mask-table entry 8, transcribed from the generated SMC boot firmware. It is the one
// programmed window on this part that translates with a live rebase, so it is what the arithmetic
// below is anchored to. UMD does not resolve through it; it is here as an independent oracle.
constexpr att::Window ATHENA_LPDDR2_WINDOW{
    .compare = 0x6000000000000ULL,
    .mask_bits = 49,
    .endpoint_shift = 48,
    .endpoint_size = 1,
    .endpoint_table_offset = 39,
    .translate_address = true,
    .bar = 0x2000000000000ULL,
};

TEST(AttWindow, RebasesOntoTheDestinationRangeTheAddressMapDocuments) {
    // grendelemulation's att/address_map.md gives both sides of this window independently: the
    // global range 0x6000000000000..0x6ffffffffffff carries the destination's own PCIE IO range
    // 0x2000000000000..0x2ffffffffffff. Both ends must land exactly, or the rebase is wrong.
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.make_address(0, 0x2000000000000ULL), 0x6000000000000ULL);
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.make_address(0, 0x2ffffffffffffULL), 0x6ffffffffffffULL);

    // A destination address partway in lands at the same distance into the global window.
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.make_address(0, 0x2000000001234ULL), 0x6000000001234ULL);

    // Decoded back, the same documented endpoints must reappear. This direction is what pins the
    // rebase: this window's bar is a bit compare already carries, so composing an address by OR
    // gives the same answer whether or not it was subtracted.
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.local_address(0x6000000000000ULL), 0x2000000000000ULL);
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.local_address(0x6ffffffffffffULL), 0x2ffffffffffffULL);
}

TEST(AttWindow, DecodesARebasedAddressBackToTheDestinationOffset) {
    const uint64_t address = ATHENA_LPDDR2_WINDOW.make_address(0, 0x2000000001234ULL);

    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.local_address(address), 0x2000000001234ULL);
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.selector(address), 0u);
}

TEST(AttWindow, ARebasedWindowStartsAtItsBarRatherThanAtZero) {
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.offset_base(), 0x2000000000000ULL);
    EXPECT_EQ(ATHENA_LPDDR2_WINDOW.offset_limit(), 0x3000000000000ULL);

    // Below the rebase there is no address to build: the local field would wrap.
    EXPECT_FALSE(ATHENA_LPDDR2_WINDOW.transfer_supported(0x1ffffffffffffULL, 4));
    EXPECT_FALSE(ATHENA_LPDDR2_WINDOW.transfer_supported(0, 4));
}

TEST(AttWindow, BoundsARebasedTransferAgainstTheShiftedSlot) {
    EXPECT_TRUE(ATHENA_LPDDR2_WINDOW.transfer_supported(0x2000000000000ULL, 4));

    // The last four bytes of the slot fit; anything crossing its end does not.
    EXPECT_TRUE(ATHENA_LPDDR2_WINDOW.transfer_supported(0x2fffffffffffcULL, 4));
    EXPECT_FALSE(ATHENA_LPDDR2_WINDOW.transfer_supported(0x2fffffffffffcULL, 8));
    EXPECT_FALSE(ATHENA_LPDDR2_WINDOW.transfer_supported(0x3000000000000ULL, 1));
}

// qsr.s1 mask-table entry 5: the firmware programmes a bar, but the window is pass-through.
constexpr att::Window PASS_THROUGH_WINDOW_WITH_BAR{
    .compare = 0x1000000000000ULL,
    .mask_bits = 38,
    .endpoint_shift = 33,
    .endpoint_size = 5,
    .endpoint_table_offset = 96,
    .translate_address = false,
    .bar = 0x800000000ULL,
};

TEST(AttWindow, IgnoresTheBarOfAPassThroughWindow) {
    // Hardware forwards the whole address and never adds the rebase, so offsets still start at
    // zero. Subtracting it here would put every GDDR access 32 GiB below where it was asked for.
    EXPECT_EQ(PASS_THROUGH_WINDOW_WITH_BAR.offset_base(), 0u);
    EXPECT_EQ(PASS_THROUGH_WINDOW_WITH_BAR.make_address(0, 0), 0x1000000000000ULL);
    EXPECT_EQ(PASS_THROUGH_WINDOW_WITH_BAR.make_address(0, 0x2000), 0x1000000002000ULL);
    EXPECT_TRUE(PASS_THROUGH_WINDOW_WITH_BAR.transfer_supported(0, 4));
}
