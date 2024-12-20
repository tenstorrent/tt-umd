/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gtest/gtest.h"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/grayskull_implementation.h"
#include "umd/device/tt_soc_descriptor.h"
#include "umd/device/wormhole_implementation.h"

using namespace tt::umd;

// Test soc descriptor API for Wormhole when there is no harvesting.
TEST(SocDescriptor, SocDescriptorGrayskullNoHarvesting) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"));

    const std::vector<tt_xy_pair> grayskull_tensix_cores = tt::umd::grayskull::TENSIX_CORES;

    ASSERT_EQ(soc_desc.get_num_dram_channels(), tt::umd::grayskull::NUM_DRAM_BANKS);

    for (const tt_xy_pair& tensix_core : grayskull_tensix_cores) {
        ASSERT_TRUE(soc_desc.is_worker_core(tensix_core));
        ASSERT_FALSE(soc_desc.is_ethernet_core(tensix_core));
    }

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::TENSIX).empty());
    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
}

// Test soc descriptor API for Grayskull when there is tensix harvesting.
TEST(SocDescriptor, SocDescriptorGrayskullOneRowHarvesting) {
    const tt_xy_pair grayskull_tensix_grid_size = tt::umd::grayskull::TENSIX_GRID_SIZE;
    const std::vector<tt_xy_pair> grayskull_tensix_cores = tt::umd::grayskull::TENSIX_CORES;
    const size_t harvesting_mask = (1 << tt::umd::grayskull::LOGICAL_HARVESTING_LAYOUT[0]);

    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"), harvesting_mask);

    const std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);

    ASSERT_EQ(tensix_cores.size(), grayskull_tensix_grid_size.x * (grayskull_tensix_grid_size.y - 1));

    size_t index = grayskull_tensix_grid_size.x;

    for (size_t core_index = 0; core_index < tensix_cores.size(); core_index++) {
        ASSERT_EQ(tensix_cores[core_index].x, grayskull_tensix_cores[index].x);
        ASSERT_EQ(tensix_cores[core_index].y, grayskull_tensix_cores[index].y);
        index++;
    }

    const std::vector<CoreCoord> harvested_cores = soc_desc.get_harvested_cores(CoreType::TENSIX);

    ASSERT_FALSE(harvested_cores.empty());

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
}

// Test soc descriptor API for getting DRAM cores.
TEST(SocDescriptor, SocDescriptorGrayskullDRAM) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"));

    const std::vector<std::vector<CoreCoord>> dram_cores = soc_desc.get_dram_cores();

    ASSERT_EQ(dram_cores.size(), tt::umd::grayskull::NUM_DRAM_BANKS);
    for (auto& vec : dram_cores) {
        ASSERT_EQ(vec.size(), tt::umd::grayskull::NUM_NOC_PORTS_PER_DRAM_BANK);
    }
}

// Test soc descriptor API for Wormhole when there is no harvesting.
TEST(SocDescriptor, SocDescriptorWormholeNoHarvesting) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"));

    const std::vector<tt_xy_pair> wormhole_tensix_cores = tt::umd::wormhole::TENSIX_CORES;

    ASSERT_EQ(soc_desc.get_num_dram_channels(), tt::umd::wormhole::NUM_DRAM_BANKS);

    for (const tt_xy_pair& tensix_core : wormhole_tensix_cores) {
        ASSERT_TRUE(soc_desc.is_worker_core(tensix_core));
        ASSERT_FALSE(soc_desc.is_ethernet_core(tensix_core));
    }

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::TENSIX).empty());
    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
}

// Test soc descriptor API for getting DRAM cores.
TEST(SocDescriptor, SocDescriptorWormholeDRAM) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"));

    const std::vector<std::vector<CoreCoord>> dram_cores = soc_desc.get_dram_cores();

    ASSERT_EQ(dram_cores.size(), tt::umd::wormhole::NUM_DRAM_BANKS);
    for (auto& vec : dram_cores) {
        ASSERT_EQ(vec.size(), tt::umd::wormhole::NUM_NOC_PORTS_PER_DRAM_BANK);
    }
}

// Test soc descriptor API for Wormhole when there is tensix harvesting.
TEST(SocDescriptor, SocDescriptorWormholeOneRowHarvesting) {
    const tt_xy_pair wormhole_tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;
    const std::vector<tt_xy_pair> wormhole_tensix_cores = tt::umd::wormhole::TENSIX_CORES;
    const size_t harvesting_mask = (1 << tt::umd::wormhole::LOGICAL_HARVESTING_LAYOUT[0]);

    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), harvesting_mask);

    const std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);

    ASSERT_EQ(tensix_cores.size(), wormhole_tensix_grid_size.x * (wormhole_tensix_grid_size.y - 1));

    size_t index = wormhole_tensix_grid_size.x;

    for (size_t core_index = 0; core_index < tensix_cores.size(); core_index++) {
        ASSERT_EQ(tensix_cores[core_index].x, wormhole_tensix_cores[index].x);
        ASSERT_EQ(tensix_cores[core_index].y, wormhole_tensix_cores[index].y);
        index++;
    }

    const std::vector<CoreCoord> harvested_cores = soc_desc.get_harvested_cores(CoreType::TENSIX);

    ASSERT_FALSE(harvested_cores.empty());

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
}

// Test ETH translation from logical to physical coordinates.
TEST(SocDescriptor, SocDescriptorWormholeETHLogicalToPhysical) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"));

    const std::vector<tt_xy_pair>& wormhole_eth_cores = tt::umd::wormhole::ETH_CORES;
    const tt_xy_pair eth_grid_size = soc_desc.get_grid_size(CoreType::ETH);
    const std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);

    size_t index = 0;
    for (size_t y = 0; y < eth_grid_size.y; y++) {
        for (size_t x = 0; x < eth_grid_size.x; x++) {
            const CoreCoord eth_logical = CoreCoord(x, y, CoreType::ETH, CoordSystem::LOGICAL);
            const CoreCoord eth_physical = soc_desc.translate_coord_to(eth_logical, CoordSystem::PHYSICAL);
            const CoreCoord eth_virtual = soc_desc.translate_coord_to(eth_logical, CoordSystem::VIRTUAL);

            EXPECT_EQ(eth_physical.x, wormhole_eth_cores[index].x);
            EXPECT_EQ(eth_physical.y, wormhole_eth_cores[index].y);

            EXPECT_EQ(eth_virtual.x, wormhole_eth_cores[index].x);
            EXPECT_EQ(eth_virtual.y, wormhole_eth_cores[index].y);

            EXPECT_EQ(eth_cores[index].x, wormhole_eth_cores[index].x);
            EXPECT_EQ(eth_cores[index].y, wormhole_eth_cores[index].y);

            index++;
        }
    }
}

// Test soc descriptor API for Blackhole when there is no harvesting.
TEST(SocDescriptor, SocDescriptorBlackholeNoHarvesting) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml"));

    const std::vector<tt_xy_pair> blackhole_tensix_cores = tt::umd::blackhole::TENSIX_CORES;

    ASSERT_EQ(soc_desc.get_num_dram_channels(), tt::umd::blackhole::NUM_DRAM_BANKS);

    for (const tt_xy_pair& tensix_core : blackhole_tensix_cores) {
        ASSERT_TRUE(soc_desc.is_worker_core(tensix_core));
        ASSERT_FALSE(soc_desc.is_ethernet_core(tensix_core));
    }

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::TENSIX).empty());
    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
}

// Test soc descriptor API for Blackhole when there is tensix harvesting.
TEST(SocDescriptor, SocDescriptorBlackholeOneRowHarvesting) {
    const tt_xy_pair blackhole_tensix_grid_size = tt::umd::blackhole::TENSIX_GRID_SIZE;
    const std::vector<tt_xy_pair> blackhole_tensix_cores = tt::umd::blackhole::TENSIX_CORES;

    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml"), 1);

    const std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);

    ASSERT_EQ(tensix_cores.size(), (blackhole_tensix_grid_size.x - 1) * blackhole_tensix_grid_size.y);

    size_t index = 1;

    for (size_t core_index = 0; core_index < tensix_cores.size(); core_index++) {
        ASSERT_EQ(tensix_cores[core_index].x, blackhole_tensix_cores[index].x);
        ASSERT_EQ(tensix_cores[core_index].y, blackhole_tensix_cores[index].y);
        index++;
        if (index % blackhole_tensix_grid_size.x == 0) {
            index++;
        }
    }

    const std::vector<CoreCoord> harvested_cores = soc_desc.get_harvested_cores(CoreType::TENSIX);

    ASSERT_FALSE(harvested_cores.empty());

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
}

// Test soc descriptor API for getting DRAM cores.
TEST(SocDescriptor, SocDescriptorBlackholeDRAM) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml"));

    const std::vector<std::vector<CoreCoord>> dram_cores = soc_desc.get_dram_cores();

    ASSERT_EQ(dram_cores.size(), tt::umd::blackhole::NUM_DRAM_BANKS);
    for (auto& vec : dram_cores) {
        ASSERT_EQ(vec.size(), tt::umd::blackhole::NUM_NOC_PORTS_PER_DRAM_BANK);
    }
}

// Test soc descriptor API for Blackhole when there is DRAM harvesting.
TEST(SocDescriptor, SocDescriptorBlackholeDRAMHarvesting) {
    const tt_xy_pair blackhole_tensix_grid_size = tt::umd::blackhole::TENSIX_GRID_SIZE;
    const std::vector<tt_xy_pair> blackhole_tensix_cores = tt::umd::blackhole::TENSIX_CORES;
    const std::vector<tt_xy_pair> blackhole_dram_cores = tt::umd::blackhole::DRAM_CORES;
    const size_t num_dram_banks = tt::umd::blackhole::NUM_DRAM_BANKS;
    const size_t num_noc_ports_per_bank = tt::umd::blackhole::NUM_NOC_PORTS_PER_DRAM_BANK;

    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml"), 0, 1);

    const std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);

    ASSERT_EQ(tensix_cores.size(), blackhole_tensix_grid_size.x * blackhole_tensix_grid_size.y);

    size_t index = 0;
    for (size_t core_index = 0; core_index < tensix_cores.size(); core_index++) {
        ASSERT_EQ(tensix_cores[core_index].x, blackhole_tensix_cores[index].x);
        ASSERT_EQ(tensix_cores[core_index].y, blackhole_tensix_cores[index].y);
        index++;
    }

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::TENSIX).empty());

    const std::vector<CoreCoord> dram_cores = soc_desc.get_cores(CoreType::DRAM);

    ASSERT_EQ(dram_cores.size(), (num_dram_banks - 1) * num_noc_ports_per_bank);

    const std::vector<CoreCoord> harvested_dram_cores = soc_desc.get_harvested_cores(CoreType::DRAM);

    ASSERT_EQ(harvested_dram_cores.size(), num_noc_ports_per_bank);

    for (size_t core_index = 0; core_index < num_noc_ports_per_bank; core_index++) {
        ASSERT_EQ(harvested_dram_cores[core_index].x, blackhole_dram_cores[core_index].x);
        ASSERT_EQ(harvested_dram_cores[core_index].y, blackhole_dram_cores[core_index].y);
    }
}

TEST(SocDescriptor, CustomSocDescriptor) {
    tt_SocDescriptor soc_desc(test_utils::GetAbsPath("tests/soc_descs/blackhole_simulation_1x2.yaml"), 0, 0);

    const CoreCoord tensix_core_01 = CoreCoord(0, 1, CoreType::TENSIX, CoordSystem::PHYSICAL);
    const CoreCoord tensix_core_01_virtual = soc_desc.translate_coord_to(tensix_core_01, CoordSystem::VIRTUAL);
    const CoreCoord tensix_core_01_logical = soc_desc.translate_coord_to(tensix_core_01, CoordSystem::LOGICAL);
    const CoreCoord tensix_core_01_translated = soc_desc.translate_coord_to(tensix_core_01, CoordSystem::TRANSLATED);

    EXPECT_EQ(tensix_core_01_virtual.x, tensix_core_01.x);
    EXPECT_EQ(tensix_core_01_virtual.y, tensix_core_01.y);

    EXPECT_EQ(tensix_core_01_virtual.x, tensix_core_01_translated.x);
    EXPECT_EQ(tensix_core_01_virtual.y, tensix_core_01_translated.y);

    EXPECT_EQ(tensix_core_01_logical.x, 0);
    EXPECT_EQ(tensix_core_01_logical.y, 0);

    const CoreCoord tensix_core_11 = CoreCoord(1, 1, CoreType::TENSIX, CoordSystem::PHYSICAL);
    const CoreCoord tensix_core_11_virtual = soc_desc.translate_coord_to(tensix_core_11, CoordSystem::VIRTUAL);
    const CoreCoord tensix_core_11_logical = soc_desc.translate_coord_to(tensix_core_11, CoordSystem::LOGICAL);
    const CoreCoord tensix_core_11_translated = soc_desc.translate_coord_to(tensix_core_11, CoordSystem::TRANSLATED);

    EXPECT_EQ(tensix_core_11_virtual.x, tensix_core_11.x);
    EXPECT_EQ(tensix_core_11_virtual.y, tensix_core_11.y);

    EXPECT_EQ(tensix_core_11_virtual.x, tensix_core_11_translated.x);
    EXPECT_EQ(tensix_core_11_virtual.y, tensix_core_11_translated.y);

    EXPECT_EQ(tensix_core_11_logical.x, 1);
    EXPECT_EQ(tensix_core_11_logical.y, 0);

    std::vector<CoreCoord> cores = soc_desc.get_cores(CoreType::TENSIX);
    EXPECT_EQ(cores.size(), 2);

    EXPECT_EQ(cores[0], tensix_core_01);
    EXPECT_EQ(cores[1], tensix_core_11);

    std::vector<CoreCoord> harvested_tensix_cores = soc_desc.get_harvested_cores(CoreType::TENSIX);
    EXPECT_TRUE(harvested_tensix_cores.empty());

    const CoreCoord dram_core_10 = CoreCoord(1, 0, CoreType::DRAM, CoordSystem::PHYSICAL);
    const CoreCoord dram_core_10_virtual = soc_desc.translate_coord_to(dram_core_10, CoordSystem::VIRTUAL);
    const CoreCoord dram_core_10_logical = soc_desc.translate_coord_to(dram_core_10, CoordSystem::LOGICAL);
    const CoreCoord dram_core_10_translated = soc_desc.translate_coord_to(dram_core_10, CoordSystem::TRANSLATED);

    EXPECT_EQ(dram_core_10_virtual.x, dram_core_10.x);
    EXPECT_EQ(dram_core_10_virtual.y, dram_core_10.y);

    EXPECT_EQ(dram_core_10.x, dram_core_10_translated.x);
    EXPECT_EQ(dram_core_10.y, dram_core_10_translated.y);

    EXPECT_EQ(dram_core_10_logical.x, 0);
    EXPECT_EQ(dram_core_10_logical.y, 0);

    EXPECT_EQ(soc_desc.get_num_dram_channels(), 1);
}
