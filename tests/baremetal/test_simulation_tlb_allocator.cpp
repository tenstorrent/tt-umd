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

// Wormhole TLB pool layout (indices and sizes match SimulationTlbAllocator's
// initialize_architecture_config for tt::ARCH::WORMHOLE_B0).
constexpr size_t WH_NUM_1MB_TLBS = 156;
constexpr size_t WH_NUM_2MB_TLBS = 10;
constexpr size_t WH_TLB_1MB_SIZE = 1ULL << 20;
constexpr size_t WH_TLB_2MB_SIZE = 2ULL << 20;
constexpr size_t WH_TLB_16MB_SIZE = 16ULL << 20;
constexpr size_t WH_TLB_REG_STRIDE = 8;

// Blackhole TLB pool layout.
constexpr size_t BH_NUM_2MB_TLBS = 202;
constexpr size_t BH_TLB_2MB_SIZE = 2ULL << 20;
constexpr size_t BH_TLB_REG_STRIDE = 12;

// Arbitrary nonzero base for address-math assertions.
constexpr uint64_t TEST_BAR0_BASE = 0x10'000'000ULL;

// TLB configuration registers start at this offset from BAR0 base
// (see SimulationTlbAllocator::get_tlb_reg_address_from_index).
constexpr uint64_t TLB_CONFIG_REG_BASE_OFFSET = 0x1fc00000;

}  // namespace

TEST(SimulationTlbAllocator, WormholeBasicAllocateAndDeallocate) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    int idx = allocator.allocate_tlb_index(WH_TLB_1MB_SIZE);
    ASSERT_NE(idx, -1);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, static_cast<int>(WH_NUM_1MB_TLBS));
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), WH_TLB_1MB_SIZE);

    allocator.deallocate_tlb_index(idx);

    // After deallocation the same index can be returned again.
    int idx2 = allocator.allocate_tlb_index(WH_TLB_1MB_SIZE);
    ASSERT_NE(idx2, -1);
    EXPECT_EQ(idx2, idx);
}

TEST(SimulationTlbAllocator, WormholeAllocateEachSizeClass) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    int idx_1mb = allocator.allocate_tlb_index(WH_TLB_1MB_SIZE);
    ASSERT_NE(idx_1mb, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx_1mb), WH_TLB_1MB_SIZE);

    int idx_2mb = allocator.allocate_tlb_index(WH_TLB_2MB_SIZE);
    ASSERT_NE(idx_2mb, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx_2mb), WH_TLB_2MB_SIZE);

    int idx_16mb = allocator.allocate_tlb_index(WH_TLB_16MB_SIZE);
    ASSERT_NE(idx_16mb, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx_16mb), WH_TLB_16MB_SIZE);
}

TEST(SimulationTlbAllocator, WormholeAllocateZeroPicksSmallest) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    int idx = allocator.allocate_tlb_index(0);
    ASSERT_NE(idx, -1);
    // Smallest size class on Wormhole is 1MB, indices 0..155.
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), WH_TLB_1MB_SIZE);
}

TEST(SimulationTlbAllocator, WormholeIndicesAreUniqueWithinSizeClass) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    std::unordered_set<int> seen_indices;
    seen_indices.reserve(WH_NUM_1MB_TLBS);
    for (size_t i = 0; i < WH_NUM_1MB_TLBS; ++i) {
        int idx = allocator.allocate_tlb_index(WH_TLB_1MB_SIZE);
        ASSERT_NE(idx, -1) << "exhausted at iteration " << i;
        ASSERT_TRUE(seen_indices.insert(idx).second) << "duplicate index " << idx;
        ASSERT_EQ(allocator.get_tlb_size_from_index(idx), WH_TLB_1MB_SIZE);
    }
}

TEST(SimulationTlbAllocator, WormholeFallsBackToLargerSizeClassWhenExhausted) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // Drain the 1MB pool entirely.
    for (size_t i = 0; i < WH_NUM_1MB_TLBS; ++i) {
        ASSERT_NE(allocator.allocate_tlb_index(WH_TLB_1MB_SIZE), -1);
    }

    // A 1MB request now upgrades to a 2MB index because 1MB <= 2MB.
    int upgraded = allocator.allocate_tlb_index(WH_TLB_1MB_SIZE);
    ASSERT_NE(upgraded, -1);
    EXPECT_EQ(allocator.get_tlb_size_from_index(upgraded), WH_TLB_2MB_SIZE);
}

TEST(SimulationTlbAllocator, WormholeReturnsNegativeOneWhenAllPoolsExhausted) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // Exhaust every size class. allocate_tlb_index(0) picks any available TLB.
    while (allocator.allocate_tlb_index(0) != -1) {
        // keep allocating.
    }

    EXPECT_EQ(allocator.allocate_tlb_index(WH_TLB_1MB_SIZE), -1);
    EXPECT_EQ(allocator.allocate_tlb_index(0), -1);
}

TEST(SimulationTlbAllocator, WormholeAddressFromIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // First 1MB TLB sits at bar0 base.
    EXPECT_EQ(allocator.get_tlb_address_from_index(0), TEST_BAR0_BASE);
    // Last 1MB TLB.
    EXPECT_EQ(
        allocator.get_tlb_address_from_index(WH_NUM_1MB_TLBS - 1),
        TEST_BAR0_BASE + (WH_NUM_1MB_TLBS - 1) * WH_TLB_1MB_SIZE);
    // First 2MB TLB sits right after the 1MB region.
    const uint64_t end_of_1mb_region = TEST_BAR0_BASE + WH_NUM_1MB_TLBS * WH_TLB_1MB_SIZE;
    EXPECT_EQ(allocator.get_tlb_address_from_index(WH_NUM_1MB_TLBS), end_of_1mb_region);
    // First 16MB TLB sits right after the 2MB region.
    const uint64_t end_of_2mb_region = end_of_1mb_region + WH_NUM_2MB_TLBS * WH_TLB_2MB_SIZE;
    EXPECT_EQ(allocator.get_tlb_address_from_index(WH_NUM_1MB_TLBS + WH_NUM_2MB_TLBS), end_of_2mb_region);
}

TEST(SimulationTlbAllocator, WormholeRegAddressFromIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(0), TEST_BAR0_BASE + TLB_CONFIG_REG_BASE_OFFSET);
    EXPECT_EQ(
        allocator.get_tlb_reg_address_from_index(7),
        TEST_BAR0_BASE + TLB_CONFIG_REG_BASE_OFFSET + 7 * WH_TLB_REG_STRIDE);
}

TEST(SimulationTlbAllocator, BlackholeBasicAllocate) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::BLACKHOLE);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    int idx = allocator.allocate_tlb_index(BH_TLB_2MB_SIZE);
    ASSERT_NE(idx, -1);
    EXPECT_GE(idx, 0);
    EXPECT_LT(idx, static_cast<int>(BH_NUM_2MB_TLBS));
    EXPECT_EQ(allocator.get_tlb_size_from_index(idx), BH_TLB_2MB_SIZE);
}

TEST(SimulationTlbAllocator, BlackholeAddressFromIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::BLACKHOLE);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    EXPECT_EQ(allocator.get_tlb_address_from_index(0), TEST_BAR0_BASE);
    EXPECT_EQ(
        allocator.get_tlb_address_from_index(BH_NUM_2MB_TLBS - 1),
        TEST_BAR0_BASE + (BH_NUM_2MB_TLBS - 1) * BH_TLB_2MB_SIZE);
}

TEST(SimulationTlbAllocator, BlackholeRegAddressFromIndex) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::BLACKHOLE);
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, arch_impl.get());

    // Blackhole uses a 12-byte register stride.
    EXPECT_EQ(allocator.get_tlb_reg_address_from_index(0), TEST_BAR0_BASE + TLB_CONFIG_REG_BASE_OFFSET);
    EXPECT_EQ(
        allocator.get_tlb_reg_address_from_index(5),
        TEST_BAR0_BASE + TLB_CONFIG_REG_BASE_OFFSET + 5 * BH_TLB_REG_STRIDE);
}

TEST(SimulationTlbAllocator, GetArchitectureImpl) {
    auto arch_impl = architecture_implementation::create(tt::ARCH::WORMHOLE_B0);
    const architecture_implementation* raw = arch_impl.get();
    SimulationTlbAllocator allocator(TEST_BAR0_BASE, raw);

    EXPECT_EQ(allocator.get_architecture_impl(), raw);
}
