// SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <unordered_set>

#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/types/arch.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// Arbitrary nonzero base for address-math assertions.
constexpr uint64_t TEST_BAR0_BASE = 0x10000000ULL;

}  // namespace

TEST(SimulationTlbAllocator, WormholeBasicAllocateAndDeallocate) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    int idx = allocator.allocate_tlb_index(0x100000);
    ASSERT_NE(idx, -1);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, 156);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), 0x100000);

    allocator.deallocate_tlb_index(idx);

    // After deallocation the same index can be returned again.
    int idx2 = allocator.allocate_tlb_index(0x100000);
    ASSERT_NE(idx2, -1);
    EXPECT_EQ(idx2, idx);
}

TEST(SimulationTlbAllocator, WormholeAllocateEachSizeClass) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    int idx_1mb = allocator.allocate_tlb_index(0x100000);
    ASSERT_NE(idx_1mb, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx_1mb), 0x100000);

    int idx_2mb = allocator.allocate_tlb_index(0x200000);
    ASSERT_NE(idx_2mb, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx_2mb), 0x200000);

    int idx_16mb = allocator.allocate_tlb_index(0x1000000);
    ASSERT_NE(idx_16mb, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx_16mb), 0x1000000);
}

TEST(SimulationTlbAllocator, WormholeAllocateZeroPicksSmallest) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    int idx = allocator.allocate_tlb_index(0);
    ASSERT_NE(idx, -1);
    // Smallest size class on Wormhole is 1MB.
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), 0x100000);
}

TEST(SimulationTlbAllocator, WormholeAllocateZeroFallsBackWhenSmallestClassExhausted) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    // Drain the 1MB pool entirely (Wormhole has 156 1MB TLBs).
    for (size_t i = 0; i < 156; ++i) {
        ASSERT_NE(allocator.allocate_tlb_index(0x100000), -1);
    }

    // size == 0 should fall through to the next non-empty class (2MB) rather than fail.
    int idx = allocator.allocate_tlb_index(0);
    ASSERT_NE(idx, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), 0x200000);
}

TEST(SimulationTlbAllocator, WormholeIndicesAreUniqueWithinSizeClass) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    std::unordered_set<int> seen_indices;
    seen_indices.reserve(156);
    for (size_t i = 0; i < 156; ++i) {
        int idx = allocator.allocate_tlb_index(0x100000);
        ASSERT_NE(idx, -1) << "exhausted at iteration " << i;
        ASSERT_TRUE(seen_indices.insert(idx).second) << "duplicate index " << idx;
        ASSERT_EQ(allocator.get_tlb_size_from_index(idx), 0x100000);
    }
}

TEST(SimulationTlbAllocator, WormholeFallsBackToLargerSizeClassWhenExhausted) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    // Drain the 1MB pool entirely (Wormhole has 156 1MB TLBs).
    for (size_t i = 0; i < 156; ++i) {
        ASSERT_NE(allocator.allocate_tlb_index(0x100000), -1);
    }

    // A 1MB request now upgrades to a 2MB index because 1MB <= 2MB.
    int upgraded = allocator.allocate_tlb_index(0x100000);
    ASSERT_NE(upgraded, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(upgraded), 0x200000);
}

TEST(SimulationTlbAllocator, WormholeReturnsNegativeOneWhenAllPoolsExhausted) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    // Exhaust every size class. allocate_tlb_index(0) picks any available TLB.
    while (allocator.allocate_tlb_index(0) != -1) {
        // keep allocating.
    }

    EXPECT_EQ(allocator.allocate_tlb_index(0x100000), -1);
    EXPECT_EQ(allocator.allocate_tlb_index(0), -1);
}

TEST(SimulationTlbAllocator, WormholeAddressFromIndex) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    // Wormhole layout from BAR0 base 0x10000000:
    //   indices   0..155: 156 x 1MB  (0x10000000 .. 0x19BFFFFF)
    //   indices 156..165: 10  x 2MB  (0x19C00000 .. 0x1AFFFFFF)
    //   indices 166..185: 20  x 16MB (0x1B000000 .. 0x2AFFFFFF)
    EXPECT_EQ(allocator.get_tlb_address_from_index(0), 0x10000000ULL);
    EXPECT_EQ(allocator.get_tlb_address_from_index(155), 0x19B00000ULL);
    EXPECT_EQ(allocator.get_tlb_address_from_index(156), 0x19C00000ULL);
    EXPECT_EQ(allocator.get_tlb_address_from_index(166), 0x1B000000ULL);
}

TEST(SimulationTlbAllocator, WormholeRegAddressFromIndex) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    // TLB config registers start at BAR0+0x1FC00000 with 8-byte stride on Wormhole.
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(0), 0x2FC00000ULL);
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(7), 0x2FC00038ULL);
}

TEST(SimulationTlbAllocator, BlackholeBasicAllocate) {
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, tt::ARCH::BLACKHOLE, &get_architecture_tlbs(tt::ARCH::BLACKHOLE));

    int idx = allocator.allocate_tlb_index(0x200000);
    ASSERT_NE(idx, -1);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, 202);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), 0x200000);
}

TEST(SimulationTlbAllocator, BlackholeAddressFromIndex) {
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, tt::ARCH::BLACKHOLE, &get_architecture_tlbs(tt::ARCH::BLACKHOLE));

    // Blackhole layout from BAR0 base 0x10000000:
    //   indices 0..201: 202 x 2MB (0x10000000 .. 0x291FFFFF)
    EXPECT_EQ(allocator.get_tlb_address_from_index(0), 0x10000000ULL);
    EXPECT_EQ(allocator.get_tlb_address_from_index(201), 0x29200000ULL);
}

TEST(SimulationTlbAllocator, BlackholeRegAddressFromIndex) {
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, tt::ARCH::BLACKHOLE, &get_architecture_tlbs(tt::ARCH::BLACKHOLE));

    // TLB config registers start at BAR0+0x1FC00000 with 12-byte stride on Blackhole.
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(0), 0x2FC00000ULL);
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(5), 0x2FC0003CULL);
}

TEST(SimulationTlbAllocator, GettersThrowOnInvalidIndex) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    // Negative indices are rejected.
    EXPECT_THROW(allocator.get_tlb_size_from_index(-1), std::exception);
    EXPECT_THROW(allocator.get_tlb_address_from_index(-1), std::exception);
    EXPECT_THROW(allocator.get_tlb_reg_address_from_index(-1), std::exception);

    // Indices past the highest valid index (Wormhole has 156+10+20 = 186 TLBs,
    // so 186 is the first invalid index) are also rejected.
    EXPECT_THROW(allocator.get_tlb_size_from_index(186), std::exception);
    EXPECT_THROW(allocator.get_tlb_address_from_index(186), std::exception);
    EXPECT_THROW(allocator.get_tlb_reg_address_from_index(186), std::exception);
}

TEST(SimulationTlbAllocator, WindowedAllocatorAddressesWindows) {
    SimulationTlbAllocator allocator(
        TEST_BAR0_BASE, tt::ARCH::WORMHOLE_B0, &get_architecture_tlbs(tt::ARCH::WORMHOLE_B0));

    EXPECT_TRUE(allocator.uses_window_addressing());
}

TEST(SimulationTlbAllocator, WindowlessAllocatorHandsOutDistinctIndices) {
    // No layout: the simulator models no TLB windows for this device, so indices are bookkeeping
    // ids only. This is the Quasar path.
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, tt::ARCH::QUASAR, nullptr);

    EXPECT_FALSE(allocator.uses_window_addressing());

    // Every request succeeds, whatever the size, and no index is handed out twice.
    std::unordered_set<int> indices;
    for (int i = 0; i < 4; ++i) {
        int idx = allocator.allocate_tlb_index(4ULL * 1024 * 1024 * 1024);
        ASSERT_NE(idx, -1);
        EXPECT_TRUE(indices.insert(idx).second);
    }

    // Freeing a bookkeeping index is a no-op rather than an error.
    EXPECT_NO_THROW(allocator.deallocate_tlb_index(0));

    // There is no window behind the index, so nothing can be said about its address or size.
    EXPECT_THROW(allocator.get_tlb_size_from_index(0), std::exception);
    EXPECT_THROW(allocator.get_tlb_address_from_index(0), std::exception);
    EXPECT_THROW(allocator.get_tlb_reg_address_from_index(0), std::exception);
}

TEST(SimulationTlbAllocator, ConsumesAnyInjectedLayout) {
    // The allocator holds no knowledge of which architectures it can serve, so it lays out a layout
    // it has no code for the same way as the ones it is used with today. Quasar is the case that
    // matters: the simulator models no windows for it, so nothing but this table entry describes it.
    const ArchitectureTlbs& tlbs = get_architecture_tlbs(tt::ARCH::QUASAR);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, tt::ARCH::QUASAR, &tlbs);

    ASSERT_TRUE(allocator.uses_window_addressing());

    const TlbSizeClass& smallest = tlbs.size_classes.front();
    int idx = allocator.allocate_tlb_index(smallest.size);
    ASSERT_NE(idx, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), smallest.size);
    EXPECT_EQ(allocator.get_tlb_address_from_index(idx), TEST_BAR0_BASE + idx * smallest.size);

    // Config register stride comes from the layout, not from a default baked into the allocator.
    EXPECT_EQ(
        allocator.get_tlb_reg_address_from_index(1), TEST_BAR0_BASE + tlbs.static_cfg_addr + tlbs.cfg_reg_size_bytes);
}
