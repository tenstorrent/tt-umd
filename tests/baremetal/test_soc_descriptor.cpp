// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/common.hpp"

using namespace tt;
using namespace tt::umd;

constexpr size_t example_eth_harvesting_mask = (1 << 8) | (1 << 5);

// Test soc descriptor API for Wormhole when there is no harvesting.
TEST(SocDescriptor, SocDescriptorWormholeNoHarvesting) {
    SocDescriptor soc_desc(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"), {.noc_translation_enabled = true});

    const std::vector<tt_xy_pair> wormhole_tensix_cores = wormhole::TENSIX_CORES_NOC0;

    ASSERT_EQ(soc_desc.get_num_dram_channels(), wormhole::NUM_DRAM_BANKS);

    for (const tt_xy_pair& tensix_core : wormhole_tensix_cores) {
        CoreCoord core_coord = soc_desc.get_coord_at(tensix_core, CoordSystem::NOC0);
        ASSERT_TRUE(core_coord.core_type == CoreType::TENSIX);
    }

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::TENSIX).empty());
    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
    ASSERT_EQ(soc_desc.get_all_cores().size(), wormhole::GRID_SIZE_X * wormhole::GRID_SIZE_Y);
    ASSERT_EQ(soc_desc.get_all_harvested_cores().size(), 0);
}

// Test soc descriptor API for getting DRAM cores.
TEST(SocDescriptor, SocDescriptorWormholeDRAM) {
    SocDescriptor soc_desc(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"), {.noc_translation_enabled = true});

    const std::vector<std::vector<CoreCoord>> dram_cores = soc_desc.get_dram_cores();

    ASSERT_EQ(dram_cores.size(), wormhole::NUM_DRAM_BANKS);
    for (auto& vec : dram_cores) {
        ASSERT_EQ(vec.size(), wormhole::NUM_NOC_PORTS_PER_DRAM_BANK);
    }
}

// Test soc descriptor API for Wormhole when there is tensix harvesting.
TEST(SocDescriptor, SocDescriptorWormholeOneRowHarvesting) {
    const tt_xy_pair wormhole_tensix_grid_size = wormhole::TENSIX_GRID_SIZE;
    const std::vector<tt_xy_pair> wormhole_tensix_cores = wormhole::TENSIX_CORES_NOC0;
    const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = (1 << 0)};

    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

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

    ASSERT_EQ(
        soc_desc.get_all_cores().size(), wormhole::GRID_SIZE_X * wormhole::GRID_SIZE_Y - wormhole::TENSIX_GRID_SIZE.x);
    ASSERT_EQ(soc_desc.get_all_harvested_cores().size(), wormhole::TENSIX_GRID_SIZE.x);
}

// Test ETH translation from logical to noc0 coordinates.
TEST(SocDescriptor, SocDescriptorWormholeETHLogicalToNOC0) {
    const SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"), {.noc_translation_enabled = true});

    const std::vector<tt_xy_pair>& wormhole_eth_cores = wormhole::ETH_CORES_NOC0;
    const uint32_t num_eth_channels = soc_desc.get_num_eth_channels();
    const std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);

    size_t index = 0;
    for (size_t eth_channel = 0; eth_channel < num_eth_channels; eth_channel++) {
        const CoreCoord eth_logical = CoreCoord(0, eth_channel, CoreType::ETH, CoordSystem::LOGICAL);
        const CoreCoord eth_noc0 = soc_desc.translate_coord_to(eth_logical, CoordSystem::NOC0);

        EXPECT_EQ(eth_noc0.x, wormhole_eth_cores[index].x);
        EXPECT_EQ(eth_noc0.y, wormhole_eth_cores[index].y);

        EXPECT_EQ(eth_cores[index].x, wormhole_eth_cores[index].x);
        EXPECT_EQ(eth_cores[index].y, wormhole_eth_cores[index].y);

        index++;
    }
}

TEST(SocDescriptor, SocDescriptorDRAMChannels) {
    SocDescriptor soc_desc(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"), {.noc_translation_enabled = true});

    int num_dram_channels = soc_desc.get_num_dram_channels();

    // Core type with no separate channels.
    EXPECT_THROW(soc_desc.get_cores(tt::CoreType::ARC, tt::CoordSystem::LOGICAL, 0), std::runtime_error);
    // Invalid channel.
    EXPECT_THROW(
        soc_desc.get_cores(tt::CoreType::DRAM, tt::CoordSystem::LOGICAL, num_dram_channels + 1), std::runtime_error);

    for (int channel = 0; channel < num_dram_channels; channel++) {
        size_t core_index = 0;
        for (auto core : soc_desc.get_cores(tt::CoreType::DRAM, tt::CoordSystem::NOC0, channel)) {
            EXPECT_EQ(core.x, wormhole::DRAM_CORES_NOC0[core_index][channel].x);
            EXPECT_EQ(core.y, wormhole::DRAM_CORES_NOC0[core_index][channel].y);
            core_index++;
        }
    }
}

// Test ETH translation from logical to noc0 coordinates.
TEST(SocDescriptor, SocDescriptorBlackholeETHHarvesting) {
    const size_t num_eth_cores = blackhole::ETH_CORES_NOC0.size();
    const size_t num_harvested_eth_cores = 2;
    const size_t num_eth_channels = blackhole::NUM_ETH_CHANNELS;
    const std::vector<tt_xy_pair> blackhole_eth_cores = blackhole::ETH_CORES_NOC0;
    for (size_t eth_harvesting_mask = 0; eth_harvesting_mask < (1 << num_eth_cores); eth_harvesting_mask++) {
        if (CoordinateManager::get_num_harvested(eth_harvesting_mask) != num_harvested_eth_cores) {
            continue;
        }

        const HarvestingMasks harvesting_masks = {.eth_harvesting_mask = eth_harvesting_mask};

        SocDescriptor soc_desc(
            test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
            {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

        const std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);

        ASSERT_EQ(soc_desc.get_all_cores().size(), blackhole::GRID_SIZE_X * blackhole::GRID_SIZE_Y - 2);
        ASSERT_EQ(soc_desc.get_all_harvested_cores().size(), 2);

        EXPECT_EQ(eth_cores.size(), num_eth_channels - num_harvested_eth_cores);

        const std::vector<CoreCoord> harvested_eth_cores = soc_desc.get_harvested_cores(CoreType::ETH);

        EXPECT_EQ(harvested_eth_cores.size(), num_harvested_eth_cores);

        size_t index_harvested = 0;
        size_t index_unharvested = 0;
        for (size_t x = 0; x < num_eth_channels; x++) {
            if (eth_harvesting_mask & (1 << x)) {
                EXPECT_EQ(harvested_eth_cores[index_harvested].x, blackhole_eth_cores[x].x);
                EXPECT_EQ(harvested_eth_cores[index_harvested].y, blackhole_eth_cores[x].y);
                index_harvested++;
            } else {
                EXPECT_EQ(eth_cores[index_unharvested].x, blackhole_eth_cores[x].x);
                EXPECT_EQ(eth_cores[index_unharvested].y, blackhole_eth_cores[x].y);
                index_unharvested++;
            }
        }
    }
}

// Test soc descriptor API for Blackhole when there is no harvesting.
TEST(SocDescriptor, SocDescriptorBlackholeNoHarvesting) {
    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"), {.noc_translation_enabled = true});

    const std::vector<tt_xy_pair> blackhole_tensix_cores = blackhole::TENSIX_CORES_NOC0;

    ASSERT_EQ(soc_desc.get_num_dram_channels(), blackhole::NUM_DRAM_BANKS);

    for (const tt_xy_pair& tensix_core : blackhole_tensix_cores) {
        CoreCoord core_coord = soc_desc.get_coord_at(tensix_core, CoordSystem::NOC0);
        ASSERT_TRUE(core_coord.core_type == CoreType::TENSIX);
    }

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::TENSIX).empty());
    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::DRAM).empty());
    ASSERT_EQ(soc_desc.get_all_cores().size(), blackhole::GRID_SIZE_X * blackhole::GRID_SIZE_Y);
    ASSERT_EQ(soc_desc.get_all_harvested_cores().size(), 0);
}

// Test soc descriptor API for Blackhole when there is tensix harvesting.
TEST(SocDescriptor, SocDescriptorBlackholeOneRowHarvesting) {
    const tt_xy_pair blackhole_tensix_grid_size = blackhole::TENSIX_GRID_SIZE;
    const std::vector<tt_xy_pair> blackhole_tensix_cores = blackhole::TENSIX_CORES_NOC0;

    const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = 1};

    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

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

    ASSERT_EQ(
        soc_desc.get_all_cores().size(),
        blackhole::GRID_SIZE_X * blackhole::GRID_SIZE_Y - blackhole::TENSIX_GRID_SIZE.y);
    ASSERT_EQ(soc_desc.get_all_harvested_cores().size(), blackhole::TENSIX_GRID_SIZE.y);
}

// Test soc descriptor API for getting DRAM cores.
TEST(SocDescriptor, SocDescriptorBlackholeDRAM) {
    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"), {.noc_translation_enabled = true});

    const std::vector<std::vector<CoreCoord>> dram_cores = soc_desc.get_dram_cores();

    ASSERT_EQ(dram_cores.size(), blackhole::NUM_DRAM_BANKS);
    for (auto& vec : dram_cores) {
        ASSERT_EQ(vec.size(), blackhole::NUM_NOC_PORTS_PER_DRAM_BANK);
    }
}

// Test soc descriptor API for Blackhole when there is DRAM harvesting.
TEST(SocDescriptor, SocDescriptorBlackholeDRAMHarvesting) {
    const tt_xy_pair blackhole_tensix_grid_size = blackhole::TENSIX_GRID_SIZE;
    const std::vector<tt_xy_pair> blackhole_tensix_cores = blackhole::TENSIX_CORES_NOC0;
    const std::vector<tt_xy_pair> blackhole_dram_cores = flatten_vector(blackhole::DRAM_CORES_NOC0);
    const size_t num_dram_banks = blackhole::NUM_DRAM_BANKS;
    const size_t num_noc_ports_per_bank = blackhole::NUM_NOC_PORTS_PER_DRAM_BANK;

    const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = 0, .dram_harvesting_mask = 1};

    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

    const std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);

    ASSERT_EQ(tensix_cores.size(), blackhole_tensix_grid_size.x * blackhole_tensix_grid_size.y);

    size_t index = 0;
    for (size_t core_index = 0; core_index < tensix_cores.size(); core_index++) {
        ASSERT_EQ(tensix_cores[core_index].x, blackhole_tensix_cores[index].x);
        ASSERT_EQ(tensix_cores[core_index].y, blackhole_tensix_cores[index].y);
        index++;
    }

    ASSERT_TRUE(soc_desc.get_harvested_cores(CoreType::TENSIX).empty());
    ASSERT_EQ(soc_desc.get_all_cores().size(), blackhole::GRID_SIZE_X * blackhole::GRID_SIZE_Y - 3);
    ASSERT_EQ(soc_desc.get_all_harvested_cores().size(), 3);

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
    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_simulation_1x2.yaml"), {.noc_translation_enabled = true});

    const CoreCoord tensix_core_01 = CoreCoord(0, 1, CoreType::TENSIX, CoordSystem::NOC0);
    const CoreCoord tensix_core_01_logical = soc_desc.translate_coord_to(tensix_core_01, CoordSystem::LOGICAL);
    const CoreCoord tensix_core_01_translated = soc_desc.translate_coord_to(tensix_core_01, CoordSystem::TRANSLATED);

    EXPECT_EQ(tensix_core_01.x, tensix_core_01_translated.x);
    EXPECT_EQ(tensix_core_01.y, tensix_core_01_translated.y);

    EXPECT_EQ(tensix_core_01_logical.x, 0);
    EXPECT_EQ(tensix_core_01_logical.y, 0);

    const CoreCoord tensix_core_11 = CoreCoord(1, 1, CoreType::TENSIX, CoordSystem::NOC0);
    const CoreCoord tensix_core_11_logical = soc_desc.translate_coord_to(tensix_core_11, CoordSystem::LOGICAL);
    const CoreCoord tensix_core_11_translated = soc_desc.translate_coord_to(tensix_core_11, CoordSystem::TRANSLATED);

    EXPECT_EQ(tensix_core_11.x, tensix_core_11_translated.x);
    EXPECT_EQ(tensix_core_11.y, tensix_core_11_translated.y);

    EXPECT_EQ(tensix_core_11_logical.x, 1);
    EXPECT_EQ(tensix_core_11_logical.y, 0);

    std::vector<CoreCoord> cores = soc_desc.get_cores(CoreType::TENSIX);
    EXPECT_EQ(cores.size(), 2);

    EXPECT_EQ(cores[0], tensix_core_01);
    EXPECT_EQ(cores[1], tensix_core_11);

    std::vector<CoreCoord> harvested_tensix_cores = soc_desc.get_harvested_cores(CoreType::TENSIX);
    EXPECT_TRUE(harvested_tensix_cores.empty());

    const CoreCoord dram_core_10 = CoreCoord(1, 0, CoreType::DRAM, CoordSystem::NOC0);
    const CoreCoord dram_core_10_logical = soc_desc.translate_coord_to(dram_core_10, CoordSystem::LOGICAL);
    const CoreCoord dram_core_10_translated = soc_desc.translate_coord_to(dram_core_10, CoordSystem::TRANSLATED);

    EXPECT_EQ(dram_core_10.x, dram_core_10_translated.x);
    EXPECT_EQ(dram_core_10.y, dram_core_10_translated.y);

    EXPECT_EQ(dram_core_10_logical.x, 0);
    EXPECT_EQ(dram_core_10_logical.y, 0);

    EXPECT_EQ(soc_desc.get_num_dram_channels(), 1);
}

TEST(SocDescriptor, SocDescriptorWormholeMultipleCoordinateSystems) {
    SocDescriptor soc_desc(test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"), {.noc_translation_enabled = true});

    const std::vector<tt_xy_pair> cores_noc0 = wormhole::TENSIX_CORES_NOC0;

    std::vector<CoreCoord> logical_from_noc0;
    std::vector<CoreCoord> translated_from_noc0;

    for (const tt_xy_pair& noc0_core : cores_noc0) {
        const CoreCoord core(noc0_core.x, noc0_core.y, CoreType::TENSIX, CoordSystem::NOC0);
        logical_from_noc0.push_back(soc_desc.translate_coord_to(core, CoordSystem::LOGICAL));
        translated_from_noc0.push_back(soc_desc.translate_coord_to(core, CoordSystem::TRANSLATED));
    }

    std::vector<CoreCoord> cores_logical = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::LOGICAL);
    std::vector<CoreCoord> cores_translated = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);

    EXPECT_TRUE(logical_from_noc0 == cores_logical);
    EXPECT_TRUE(translated_from_noc0 == cores_translated);
}

TEST(SocDescriptor, SocDescriptorBlackholeMultipleCoordinateSystems) {
    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"), {.noc_translation_enabled = true});

    const std::vector<tt_xy_pair> cores_noc0 = blackhole::TENSIX_CORES_NOC0;

    std::vector<CoreCoord> logical_from_noc0;
    std::vector<CoreCoord> translated_from_noc0;

    for (const tt_xy_pair& noc0_core : cores_noc0) {
        const CoreCoord core(noc0_core.x, noc0_core.y, CoreType::TENSIX, CoordSystem::NOC0);
        logical_from_noc0.push_back(soc_desc.translate_coord_to(core, CoordSystem::LOGICAL));
        translated_from_noc0.push_back(soc_desc.translate_coord_to(core, CoordSystem::TRANSLATED));
    }

    std::vector<CoreCoord> cores_logical = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::LOGICAL);
    std::vector<CoreCoord> cores_translated = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);

    EXPECT_TRUE(logical_from_noc0 == cores_logical);
    EXPECT_TRUE(translated_from_noc0 == cores_translated);
}

TEST(SocDescriptor, SocDescriptorWormholeNoLogicalForHarvestedCores) {
    const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = 1};
    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

    EXPECT_THROW(soc_desc.get_harvested_cores(CoreType::TENSIX, CoordSystem::LOGICAL), std::runtime_error);

    EXPECT_THROW(soc_desc.get_harvested_cores(CoreType::DRAM, CoordSystem::LOGICAL), std::runtime_error);

    EXPECT_THROW(soc_desc.get_harvested_cores(CoreType::ETH, CoordSystem::LOGICAL), std::runtime_error);
}

TEST(SocDescriptor, SocDescriptorBlackholeNoLogicalForHarvestedCores) {
    const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = 1};
    SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

    EXPECT_THROW(soc_desc.get_harvested_cores(CoreType::TENSIX, CoordSystem::LOGICAL), std::runtime_error);

    EXPECT_THROW(soc_desc.get_harvested_cores(CoreType::DRAM, CoordSystem::LOGICAL), std::runtime_error);

    EXPECT_THROW(soc_desc.get_harvested_cores(CoreType::ETH, CoordSystem::LOGICAL), std::runtime_error);
}

TEST(SocDescriptor, NocTranslation) {
    // Test when noc translation is disabled.
    {
        const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = 1};
        SocDescriptor soc_desc(
            test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"),
            {.noc_translation_enabled = false, .harvesting_masks = harvesting_masks});

        const CoreCoord tensix_core = CoreCoord(2, 2, CoreType::TENSIX, CoordSystem::NOC0);
        const CoreCoord tensix_core_translated = soc_desc.translate_coord_to(tensix_core, CoordSystem::TRANSLATED);

        EXPECT_EQ((tt_xy_pair)tensix_core_translated, (tt_xy_pair)tensix_core);
    }
    // Test when noc translation is enabled.
    {
        const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = 1};
        SocDescriptor soc_desc(
            test_utils::GetSocDescAbsPath("blackhole_140_arch_no_eth.yaml"),
            {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

        const CoreCoord tensix_core = CoreCoord(2, 2, CoreType::TENSIX, CoordSystem::NOC0);
        const CoreCoord tensix_core_translated = soc_desc.translate_coord_to(tensix_core, CoordSystem::TRANSLATED);

        EXPECT_NE((tt_xy_pair)tensix_core_translated, (tt_xy_pair)tensix_core);
    }
}

TEST(SocDescriptor, BoardBasedPCIE) {
    // Expect invalid configuration to throw an exception.
    EXPECT_ANY_THROW(SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
        {.noc_translation_enabled = true,
         .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask, .pcie_harvesting_mask = 0x1},
         .board_type = BoardType::P150}));
    EXPECT_ANY_THROW(SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
        {.noc_translation_enabled = true,
         .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask},
         .board_type = BoardType::P300,
         .asic_location = 0}));
    EXPECT_ANY_THROW(SocDescriptor soc_desc(
        test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
        {.noc_translation_enabled = true,
         .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask},
         .board_type = BoardType::P300,
         .asic_location = 1}));

    {
        SocDescriptor soc_desc(
            test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
            {.noc_translation_enabled = true,
             .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask, .pcie_harvesting_mask = 0x1},
             .board_type = BoardType::P100});
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE)[0].x, 11);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE)[0].x, 2);
    }

    {
        SocDescriptor soc_desc(
            test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
            {.noc_translation_enabled = true,
             .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask, .pcie_harvesting_mask = 0x2},
             .board_type = BoardType::P150});
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE)[0].x, 2);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE)[0].x, 11);
    }

    {
        SocDescriptor soc_desc(
            test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
            {.noc_translation_enabled = true,
             .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask, .pcie_harvesting_mask = 0x2},
             .board_type = BoardType::P300,
             .asic_location = 0});
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE)[0].x, 2);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE)[0].x, 11);
    }

    {
        SocDescriptor soc_desc(
            test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
            {.noc_translation_enabled = true,
             .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask, .pcie_harvesting_mask = 0x1},
             .board_type = BoardType::P300,
             .asic_location = 1});
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_cores(CoreType::PCIE)[0].x, 11);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE).size(), 1);
        EXPECT_EQ(soc_desc.get_harvested_cores(CoreType::PCIE)[0].x, 2);
    }

    // If board type is not provided, just pass through what was described by the soc descriptor.
    EXPECT_EQ(
        SocDescriptor(
            test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
            {.noc_translation_enabled = true, .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask}})
            .get_cores(CoreType::PCIE)
            .size(),
        2);
}

TEST(SocDescriptor, WormholeNOC1Cores) {
    // Harvesting mask should harvest first 2 Tensix rows.
    const uint32_t num_harvested_rows = 2;
    HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = 0x3};
    // Wormhole tensix noc1 cores with first 2 harvested rows so we can just iterate
    // over the cores without the need to calculate the index.
    // clang-format off
    static const std::vector<tt_xy_pair> TENSIX_CORES_NOC1 = {
        // {8, 10}, {7, 10}, {6, 10}, {5, 10}, {3, 10}, {2, 10}, {1, 10}, {0, 10},
        // {8, 9},   {7, 9},  {6, 9},  {5, 9},  {3, 9},  {2, 9},  {1, 9},  {0, 9},
        {8, 8},   {7, 8},  {6, 8},  {5, 8},  {3, 8},  {2, 8},  {1, 8},  {0, 8},
        {8, 7},   {7, 7},  {6, 7},  {5, 7},  {3, 7},  {2, 7},  {1, 7},  {0, 7},
        {8, 6},   {7, 6},  {6, 6},  {5, 6},  {3, 6},  {2, 6},  {1, 6},  {0, 6},
        {8, 4},   {7, 4},  {6, 4},  {5, 4},  {3, 4},  {2, 4},  {1, 4},  {0, 4},
        {8, 3},   {7, 3},  {6, 3},  {5, 3},  {3, 3},  {2, 3},  {1, 3},  {0, 3},
        {8, 2},   {7, 2},  {6, 2},  {5, 2},  {3, 2},  {2, 2},  {1, 2},  {0, 2},
        {8, 1},   {7, 1},  {6, 1},  {5, 1},  {3, 1},  {2, 1},  {1, 1},  {0, 1},
        {8, 0},   {7, 0},  {6, 0},  {5, 0},  {3, 0},  {2, 0},  {1, 0},  {0, 0},
    };
    // clang-format on

    SocDescriptor soc_desc_yaml(
        test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

    SocDescriptor soc_desc_arch(
        tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

    const std::vector<CoreCoord> tensix_cores_noc1_yaml = soc_desc_yaml.get_cores(CoreType::TENSIX, CoordSystem::NOC1);
    const std::vector<CoreCoord> tensix_cores_noc1_arch = soc_desc_arch.get_cores(CoreType::TENSIX, CoordSystem::NOC1);

    EXPECT_EQ(tensix_cores_noc1_yaml.size(), tensix_cores_noc1_arch.size());

    EXPECT_EQ(
        tensix_cores_noc1_yaml.size(),
        wormhole::TENSIX_GRID_SIZE.x * (wormhole::TENSIX_GRID_SIZE.y - num_harvested_rows));

    for (size_t i = 0; i < tensix_cores_noc1_yaml.size(); i++) {
        EXPECT_EQ(tensix_cores_noc1_yaml[i], tensix_cores_noc1_arch[i]);
    }

    // Move the index for the first 2 harvested rows.
    size_t core_coord_index = 0;
    for (const CoreCoord& tensix_core : tensix_cores_noc1_yaml) {
        const tt_xy_pair noc1_pair = TENSIX_CORES_NOC1[core_coord_index];
        EXPECT_EQ(tensix_core.x, noc1_pair.x);
        EXPECT_EQ(tensix_core.y, noc1_pair.y);
        core_coord_index++;
    }
}

TEST(SocDescriptor, BlackholeNOC1Cores) {
    // Harvesting mask should harvest first 2 Tensix columns.
    const uint32_t num_harvested_columns = 2;
    HarvestingMasks harvesting_masks = {
        .tensix_harvesting_mask = 0x3, .eth_harvesting_mask = example_eth_harvesting_mask};
    // Blackhole tensix noc1 cores with first 2 harvested columns so we can just iterate
    // over the cores without the need to calculate the index.
    // clang-format off
    const static std::vector<tt_xy_pair> TENSIX_CORES_NOC1 = {
        /*{15, 9}, {14, 9},*/ {13, 9}, {12, 9}, {11, 9}, {10, 9}, {9, 9}, {6, 9}, {5, 9}, {4, 9}, {3, 9}, {2, 9}, {1, 9}, {0, 9},
        /*{15, 8}, {14, 8},*/ {13, 8}, {12, 8}, {11, 8}, {10, 8}, {9, 8}, {6, 8}, {5, 8}, {4, 8}, {3, 8}, {2, 8}, {1, 8}, {0, 8},
        /*{15, 7}, {14, 7},*/ {13, 7}, {12, 7}, {11, 7}, {10, 7}, {9, 7}, {6, 7}, {5, 7}, {4, 7}, {3, 7}, {2, 7}, {1, 7}, {0, 7},
        /*{15, 6}, {14, 6},*/ {13, 6}, {12, 6}, {11, 6}, {10, 6}, {9, 6}, {6, 6}, {5, 6}, {4, 6}, {3, 6}, {2, 6}, {1, 6}, {0, 6},
        /*{15, 5}, {14, 5},*/ {13, 5}, {12, 5}, {11, 5}, {10, 5}, {9, 5}, {6, 5}, {5, 5}, {4, 5}, {3, 5}, {2, 5}, {1, 5}, {0, 5},
        /*{15, 4}, {14, 4},*/ {13, 4}, {12, 4}, {11, 4}, {10, 4}, {9, 4}, {6, 4}, {5, 4}, {4, 4}, {3, 4}, {2, 4}, {1, 4}, {0, 4},
        /*{15, 3}, {14, 3},*/ {13, 3}, {12, 3}, {11, 3}, {10, 3}, {9, 3}, {6, 3}, {5, 3}, {4, 3}, {3, 3}, {2, 3}, {1, 3}, {0, 3},
        /*{15, 2}, {14, 2},*/ {13, 2}, {12, 2}, {11, 2}, {10, 2}, {9, 2}, {6, 2}, {5, 2}, {4, 2}, {3, 2}, {2, 2}, {1, 2}, {0, 2},
        /*{15, 1}, {14, 1},*/ {13, 1}, {12, 1}, {11, 1}, {10, 1}, {9, 1}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {2, 1}, {1, 1}, {0, 1},
        /*{15, 0}, {14, 0},*/ {13, 0}, {12, 0}, {11, 0}, {10, 0}, {9, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}, {0, 0}
    };
    // clang-format on

    SocDescriptor soc_desc_yaml(
        test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

    SocDescriptor soc_desc_arch(
        tt::ARCH::BLACKHOLE, {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});

    const std::vector<CoreCoord> tensix_cores_noc1_yaml = soc_desc_yaml.get_cores(CoreType::TENSIX, CoordSystem::NOC1);
    const std::vector<CoreCoord> tensix_cores_noc1_arch = soc_desc_arch.get_cores(CoreType::TENSIX, CoordSystem::NOC1);

    EXPECT_EQ(tensix_cores_noc1_yaml.size(), tensix_cores_noc1_arch.size());

    EXPECT_EQ(
        tensix_cores_noc1_yaml.size(),
        blackhole::TENSIX_GRID_SIZE.y * (blackhole::TENSIX_GRID_SIZE.x - num_harvested_columns));

    for (size_t i = 0; i < tensix_cores_noc1_yaml.size(); i++) {
        EXPECT_EQ(tensix_cores_noc1_yaml[i], tensix_cores_noc1_arch[i]);
    }

    size_t core_coord_index = 0;
    for (const CoreCoord& tensix_core : tensix_cores_noc1_yaml) {
        const tt_xy_pair noc1_pair = TENSIX_CORES_NOC1[core_coord_index];
        EXPECT_EQ(tensix_core.x, noc1_pair.x);
        EXPECT_EQ(tensix_core.y, noc1_pair.y);
        core_coord_index++;
    }
}

TEST(SocDescriptor, AllSocDescriptors) {
    for (const std::string& soc_desc_yaml : test_utils::GetAllSocDescs()) {
        std::cout << "Testing " << soc_desc_yaml << std::endl;

        auto arch = SocDescriptor::get_arch_from_soc_descriptor_path(soc_desc_yaml);
        HarvestingMasks harvesting_masks = {
            .eth_harvesting_mask = (arch == tt::ARCH::BLACKHOLE) ? example_eth_harvesting_mask : 0};

        SocDescriptor soc_desc(soc_desc_yaml, {.noc_translation_enabled = true, .harvesting_masks = harvesting_masks});
    }
}

TEST(SocDescriptor, SocDescriptorWormholeNoSecurityCores) {
    SocDescriptor soc_desc_yaml(
        test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"), {.noc_translation_enabled = true});

    EXPECT_EQ(soc_desc_yaml.get_cores(CoreType::SECURITY).size(), 0);

    SocDescriptor soc_desc_arch(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    EXPECT_EQ(soc_desc_arch.get_cores(CoreType::SECURITY).size(), 0);
}

TEST(SocDescriptor, SocDescriptorBlackholeSecurity) {
    SocDescriptor soc_desc_yaml(
        test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask}});

    EXPECT_EQ(soc_desc_yaml.get_cores(CoreType::SECURITY).size(), 1);

    SocDescriptor soc_desc_arch(
        tt::ARCH::BLACKHOLE,
        {.noc_translation_enabled = true, .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask}});

    EXPECT_EQ(soc_desc_arch.get_cores(CoreType::SECURITY).size(), 1);
}

TEST(SocDescriptor, SocDescriptorWormholeNoL2CPUCores) {
    SocDescriptor soc_desc_yaml(
        test_utils::GetSocDescAbsPath("wormhole_b0_8x10.yaml"), {.noc_translation_enabled = true});

    EXPECT_EQ(soc_desc_yaml.get_cores(CoreType::L2CPU).size(), 0);

    SocDescriptor soc_desc_arch(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    EXPECT_EQ(soc_desc_arch.get_cores(CoreType::L2CPU).size(), 0);
}

TEST(SocDescriptor, SocDescriptorBlackholeL2CPU) {
    SocDescriptor soc_desc_yaml(
        test_utils::GetSocDescAbsPath("blackhole_140_arch.yaml"),
        {.noc_translation_enabled = true, .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask}});

    EXPECT_EQ(soc_desc_yaml.get_cores(CoreType::L2CPU).size(), 4);

    SocDescriptor soc_desc_arch(
        tt::ARCH::BLACKHOLE,
        {.noc_translation_enabled = true, .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask}});

    EXPECT_EQ(soc_desc_arch.get_cores(CoreType::L2CPU).size(), 4);
}

TEST(SocDescriptor, SerializeSimulatorBlackhole) {
    const SocDescriptor& soc_descriptor = SocDescriptor(
        test_utils::GetSocDescAbsPath("blackhole_simulation_1x2.yaml"),
        {.noc_translation_enabled = false, .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask}});

    std::filesystem::path file_path = soc_descriptor.serialize_to_file();
    SocDescriptor soc(
        file_path.string(),
        {.noc_translation_enabled = soc_descriptor.noc_translation_enabled,
         .harvesting_masks = soc_descriptor.harvesting_masks});
}

TEST(SocDescriptor, SerializeSimulatorQuasar) {
    const SocDescriptor& soc_descriptor = SocDescriptor(
        test_utils::GetSocDescAbsPath("quasar_simulation_1x1.yaml"),
        {.noc_translation_enabled = false, .harvesting_masks = {.eth_harvesting_mask = example_eth_harvesting_mask}});

    std::filesystem::path file_path = soc_descriptor.serialize_to_file();
    SocDescriptor soc(
        file_path.string(),
        {.noc_translation_enabled = soc_descriptor.noc_translation_enabled,
         .harvesting_masks = soc_descriptor.harvesting_masks});
}

TEST(SocDescriptor, SocDescriptorCreatFromSerialized) {
    SocDescriptor soc_desc_yaml(test_utils::GetSocDescAbsPath("serialized.yaml"), {.noc_translation_enabled = true});
}
