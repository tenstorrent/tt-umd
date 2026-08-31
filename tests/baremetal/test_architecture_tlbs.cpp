// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <string>

#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/arch.hpp"

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
