// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <string>

#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/types/arch.hpp"

using namespace tt::umd;

class ArchitectureTlbsTest : public ::testing::TestWithParam<tt::ARCH> {};

INSTANTIATE_TEST_SUITE_P(
    Architectures,
    ArchitectureTlbsTest,
    testing::Values(tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE, tt::ARCH::QUASAR),
    [](const testing::TestParamInfo<tt::ARCH>& info) { return tt::arch_to_str(info.param); });

// Expectations come from the size classes themselves rather than from architecture constants, so
// this covers which size class an index resolves to without restating the tables.
TEST_P(ArchitectureTlbsTest, ResolvesEveryWindowToTheClassOwningIt) {
    const ArchitectureTlbs& tlbs = get_architecture_tlbs(GetParam());

    for (const TlbSizeClass& size_class : tlbs.size_classes) {
        for (const uint32_t index_offset : {uint32_t{0}, size_class.count - 1}) {
            const tlb_configuration config = tlbs.get_configuration(size_class.base_index + index_offset);

            EXPECT_EQ(config.size, size_class.size) << "window " << index_offset;
            EXPECT_EQ(config.base, size_class.bar_offset) << "window " << index_offset;
            EXPECT_EQ(config.index_offset, index_offset);
            // The bit layout differs per window size, so this catches a class wired to another's.
            EXPECT_EQ(config.offset.local_offset, size_class.register_layout.local_offset);
        }
    }
}

TEST_P(ArchitectureTlbsTest, ThrowsForIndexWithoutWindow) {
    const ArchitectureTlbs& tlbs = get_architecture_tlbs(GetParam());
    const TlbSizeClass& last = tlbs.size_classes.back();

    EXPECT_THROW(tlbs.get_configuration(last.base_index + last.count), std::exception);
}

// The configuration register address is derived rather than stored, so hold that derivation against
// each architecture's own definition of it.
TEST(ArchitectureTlbs, ConfigRegisterAddressMatchesArchitectureDefinition) {
    const ArchitectureTlbs& wormhole_tlbs = get_architecture_tlbs(tt::ARCH::WORMHOLE_B0);
    EXPECT_EQ(wormhole_tlbs.get_configuration(wormhole::TLB_BASE_INDEX_1M).cfg_addr, wormhole::DYNAMIC_TLB_1M_CFG_ADDR);
    EXPECT_EQ(wormhole_tlbs.get_configuration(wormhole::TLB_BASE_INDEX_2M).cfg_addr, wormhole::DYNAMIC_TLB_2M_CFG_ADDR);
    EXPECT_EQ(
        wormhole_tlbs.get_configuration(wormhole::TLB_BASE_INDEX_16M).cfg_addr, wormhole::DYNAMIC_TLB_16M_CFG_ADDR);

    const ArchitectureTlbs& blackhole_tlbs = get_architecture_tlbs(tt::ARCH::BLACKHOLE);
    EXPECT_EQ(
        blackhole_tlbs.get_configuration(blackhole::TLB_BASE_INDEX_2M).cfg_addr, blackhole::DYNAMIC_TLB_2M_CFG_ADDR);
    EXPECT_EQ(
        blackhole_tlbs.get_configuration(blackhole::TLB_BASE_INDEX_4G).cfg_addr, blackhole::DYNAMIC_TLB_4G_CFG_ADDR);
}

// Quasar's windows are the driver's TLBAppIn0 entries, so this table has to describe the bank the
// driver actually hands out: one size, BAR0 only, nothing in BAR4.
TEST(ArchitectureTlbs, QuasarExposesOneBankOfSixteenMegabyteWindows) {
    const ArchitectureTlbs& tlbs = get_architecture_tlbs(tt::ARCH::QUASAR);

    ASSERT_EQ(tlbs.size_classes.size(), 1u);

    const TlbSizeClass& windows = tlbs.size_classes.front();
    EXPECT_EQ(windows.size, grendel::TLB_16M_SIZE);
    EXPECT_EQ(windows.size, 16ULL * 1024 * 1024);
    EXPECT_EQ(windows.count, grendel::TLB_COUNT_16M);
    EXPECT_EQ(windows.base_index, grendel::TLB_BASE_INDEX_16M);
    EXPECT_EQ(windows.bar_offset, grendel::DYNAMIC_TLB_16M_BASE);
    EXPECT_FALSE(windows.in_bar4);

    // There is no second size for a cached window to come from.
    EXPECT_EQ(tlbs.cached_window_size, windows.size);
}

// A window's BAR0 offset is its entry number scaled by the window size. The kernel driver derives
// the same offset in describe_tlb, and if the two disagree a mapped window quietly reaches the
// wrong place, so pin the derivation instead of trusting it.
TEST(ArchitectureTlbs, QuasarWindowOffsetIsItsEntryNumber) {
    const ArchitectureTlbs& tlbs = get_architecture_tlbs(tt::ARCH::QUASAR);

    // 164 is the driver's own window and 165 the first it hands out: the boundary worth naming.
    for (const uint32_t entry : {0u, 1u, 164u, 165u, grendel::TLB_COUNT_16M - 1}) {
        EXPECT_EQ(tlbs.get_configuration(entry).tlb_offset, uint64_t{entry} << grendel::TLB_16M_SHIFT)
            << "entry " << entry;
    }
}

// How a window gets programmed turns on this flag, and either way round it fails quietly: Quasar
// would store into BAR0 addresses that are not configuration registers at all, while Wormhole and
// Blackhole would hand the driver work they are perfectly able to do themselves.
TEST(ArchitectureTlbs, ConfigRegistersAreHostWritableOnlyWhereTheHostCanReachThem) {
    EXPECT_TRUE(get_architecture_tlbs(tt::ARCH::WORMHOLE_B0).cfg_registers_host_writable);
    EXPECT_TRUE(get_architecture_tlbs(tt::ARCH::BLACKHOLE).cfg_registers_host_writable);

    // Quasar keeps them behind a control plane that only the kernel driver maps.
    EXPECT_FALSE(get_architecture_tlbs(tt::ARCH::QUASAR).cfg_registers_host_writable);
}

// A caller that asks for an ordering the entry has no field for must be refused rather than told it
// got one, so the resolution of IoOrdering turns on this flag.
TEST(ArchitectureTlbs, OrderingIsCarriedOnlyWhereTheEntryHasAFieldForIt) {
    EXPECT_TRUE(get_architecture_tlbs(tt::ARCH::WORMHOLE_B0).carries_ordering);
    EXPECT_TRUE(get_architecture_tlbs(tt::ARCH::BLACKHOLE).carries_ordering);

    // A Quasar entry is a target address plus an attribute field that reaches only the NOC security
    // header; there is nowhere in it for ordering.
    EXPECT_FALSE(get_architecture_tlbs(tt::ARCH::QUASAR).carries_ordering);
}

// Zero rather than a plausible-looking address, so anything that reads these on Quasar gets an
// obviously wrong answer rather than a quietly wrong one.
TEST(ArchitectureTlbs, QuasarHasNoConfigRegisterAddress) {
    const ArchitectureTlbs& tlbs = get_architecture_tlbs(tt::ARCH::QUASAR);

    EXPECT_EQ(tlbs.static_cfg_addr, 0u);
    EXPECT_EQ(tlbs.cfg_reg_size_bytes, 0u);
}

// Whether a window can honor WindowFlags::NonCacheable turns on this, so a wrong value either
// refuses a flag the hardware could have carried or accepts one it has nowhere to put.
TEST(ArchitectureTlbs, AxiAttributesAreCarriedOnlyWhereTheEntryHasRoomForThem) {
    // A Wormhole or Blackhole TLB register has no attribute field at all.
    EXPECT_FALSE(get_architecture_tlbs(tt::ARCH::WORMHOLE_B0).carries_axi_attributes);
    EXPECT_FALSE(get_architecture_tlbs(tt::ARCH::BLACKHOLE).carries_axi_attributes);

    // Quasar's entry carries attr[4:0]: the non-cacheable bit and the AxQOS id.
    EXPECT_TRUE(get_architecture_tlbs(tt::ARCH::QUASAR).carries_axi_attributes);
}
