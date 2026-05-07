// SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <unordered_set>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/types/arch.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// Arbitrary nonzero base for address-math assertions.
constexpr uint64_t TEST_BAR0_BASE = 0x10000000ULL;

}  // namespace

TEST(SimulationTlbAllocator, WormholeBasicAllocateAndDeallocate) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

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
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

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
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    int idx = allocator.allocate_tlb_index(0);
    ASSERT_NE(idx, -1);
    // Smallest size class on Wormhole is 1MB.
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), 0x100000);
}

TEST(SimulationTlbAllocator, WormholeAllocateZeroFallsBackWhenSmallestClassExhausted) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

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
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

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
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

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
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // Exhaust every size class. allocate_tlb_index(0) picks any available TLB.
    while (allocator.allocate_tlb_index(0) != -1) {
        // keep allocating.
    }

    EXPECT_EQ(allocator.allocate_tlb_index(0x100000), -1);
    EXPECT_EQ(allocator.allocate_tlb_index(0), -1);
}

TEST(SimulationTlbAllocator, WormholeAddressFromIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

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
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // TLB config registers start at BAR0+0x1FC00000 with 8-byte stride on Wormhole.
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(0), 0x2FC00000ULL);
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(7), 0x2FC00038ULL);
}

TEST(SimulationTlbAllocator, BlackholeBasicAllocate) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::BLACKHOLE);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    int idx = allocator.allocate_tlb_index(0x200000);
    ASSERT_NE(idx, -1);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, 202);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), 0x200000);
}

TEST(SimulationTlbAllocator, BlackholeAddressFromIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::BLACKHOLE);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // Blackhole layout from BAR0 base 0x10000000:
    //   indices 0..201: 202 x 2MB (0x10000000 .. 0x291FFFFF)
    EXPECT_EQ(allocator.get_tlb_address_from_index(0), 0x10000000ULL);
    EXPECT_EQ(allocator.get_tlb_address_from_index(201), 0x29200000ULL);
}

TEST(SimulationTlbAllocator, BlackholeRegAddressFromIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::BLACKHOLE);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // TLB config registers start at BAR0+0x1FC00000 with 12-byte stride on Blackhole.
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(0), 0x2FC00000ULL);
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(5), 0x2FC0003CULL);
}

TEST(SimulationTlbAllocator, GettersThrowOnInvalidIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

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
