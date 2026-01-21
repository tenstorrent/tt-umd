// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

// Tests that, when NOC translation is enabled and there is no harvesting,
// translated and logical coordinates differ by fixed offsets.
TEST(CoordinateManager, CoordinateManagerWormholeNoHarvesting) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    // We expect full grid size since there is no harvesting.
    tt_xy_pair tensix_grid_size = wormhole::TENSIX_GRID_SIZE;
    constexpr size_t translated_x_start = wormhole::tensix_translated_coordinate_start_x;
    constexpr size_t translated_y_start = wormhole::tensix_translated_coordinate_start_y;
    for (size_t x = 0; x < tensix_grid_size.x; x++) {
        for (size_t y = 0; y < tensix_grid_size.y; y++) {
            const CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            const CoreCoord translated_coords =
                coordinate_manager->translate_coord_to(logical_coords, CoordSystem::TRANSLATED);

            EXPECT_EQ(logical_coords.x + translated_x_start, translated_coords.x);
            EXPECT_EQ(logical_coords.y + translated_y_start, translated_coords.y);
        }
    }
}

// Test basic translation to virtual and noc0 coordinates.
// We expect that the top left core will have virtual and noc0 coordinates (1, 1) and (1, 2) for
// the logical coordinates if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerWormholeTopLeftCore) {
    // This harvesting mask if targeting first row in NOC layout.
    const size_t tensix_harvesting_mask = (1 << 0);

    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {tensix_harvesting_mask});

    CoreCoord logical_coords = CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);

    // Always expect same translated coordinate for (0, 0) logical coordinate if noc_translation_enabled is true.
    CoreCoord translated_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::TRANSLATED);
    EXPECT_EQ(translated_cords, CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED));

    // This depends on harvesting mask. So expected noc0 coord is specific to this test and Wormhole arch.
    CoreCoord noc0_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::NOC0);
    EXPECT_EQ(noc0_cords, CoreCoord(1, 2, CoreType::TENSIX, CoordSystem::NOC0));
}

// Test basic translation to virtual and noc0 coordinates.
// We expect that the bottom left core will have virtual and noc0 coordinates (9, 1) and (9, 2) for
// the logical coordinates (7, 0) if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerWormholeTopRightCore) {
    // This harvesting mask if targeting first row in NOC layout.
    const size_t tensix_harvesting_mask = (1 << 0);

    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {tensix_harvesting_mask});

    tt_xy_pair tensix_grid_size = coordinate_manager->get_grid_size(CoreType::TENSIX);
    EXPECT_EQ(tensix_grid_size.x, 8);
    EXPECT_EQ(tensix_grid_size.y, 9);
    CoreCoord logical_coords = CoreCoord(tensix_grid_size.x - 1, 0, CoreType::TENSIX, CoordSystem::LOGICAL);

    CoreCoord translated_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::TRANSLATED);
    EXPECT_EQ(translated_cords, CoreCoord(25, 18, CoreType::TENSIX, CoordSystem::TRANSLATED));

    CoreCoord noc0_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::NOC0);
    EXPECT_EQ(noc0_cords, CoreCoord(9, 2, CoreType::TENSIX, CoordSystem::NOC0));
}

// Test basic translation to translated and noc0 coordinates.
// We expect that the top right core will have virtual and noc0 coordinates (18, 26) and (1, 11) for
// the logical coordinates (0, 8) if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerWormholeBottomLeftCore) {
    // This harvesting mask if targeting first row in NOC layout.
    const size_t tensix_harvesting_mask = (1 << 0);

    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {tensix_harvesting_mask});

    tt_xy_pair tensix_grid_size = coordinate_manager->get_grid_size(CoreType::TENSIX);
    EXPECT_EQ(tensix_grid_size.x, 8);
    EXPECT_EQ(tensix_grid_size.y, 9);
    CoreCoord logical_coords = CoreCoord(0, tensix_grid_size.y - 1, CoreType::TENSIX, CoordSystem::LOGICAL);

    CoreCoord translated_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::TRANSLATED);
    EXPECT_EQ(translated_cords, CoreCoord(18, 26, CoreType::TENSIX, CoordSystem::TRANSLATED));

    CoreCoord noc0_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::NOC0);
    EXPECT_EQ(noc0_cords, CoreCoord(1, 11, CoreType::TENSIX, CoordSystem::NOC0));
}

// Test logical to noc0 coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of noc0 coordinates.
// For the reverse mapping back of noc0 to logical coordinates we expect that same logical coordinates are returned
// as from original mapping.
TEST(CoordinateManager, CoordinateManagerWormholeLogicalNOC0Mapping) {
    const size_t max_num_harvested_y = 10;

    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});

        std::map<CoreCoord, CoreCoord> logical_to_noc0;
        std::set<CoreCoord> noc0_coords_set;
        tt_xy_pair tensix_grid_size = wormhole::TENSIX_GRID_SIZE;

        size_t num_harvested_y = CoordinateManager::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
                CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
                CoreCoord noc0_coords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::NOC0);
                logical_to_noc0[logical_coords] = noc0_coords;

                // Expect that logical to noc0 translation is 1-1 mapping. No duplicates for noc0 coordinates.
                EXPECT_EQ(noc0_coords_set.count(noc0_coords), 0);
                noc0_coords_set.insert(noc0_coords);
            }
        }

        // Expect that the number of noc0 coordinates is equal to the number of workers minus the number of
        // harvested rows.
        EXPECT_EQ(noc0_coords_set.size(), tensix_grid_size.x * (tensix_grid_size.y - num_harvested_y));

        for (auto it : logical_to_noc0) {
            CoreCoord noc0_coords = it.second;
            CoreCoord logical_coords = coordinate_manager->translate_coord_to(noc0_coords, CoordSystem::LOGICAL);

            // Expect that reverse mapping of noc0 coordinates gives the same logical coordinates
            // using which we got the noc0 coordinates.
            EXPECT_EQ(it.first, logical_coords);
        }
    }
}

// Test top left corner translation from logical to translated coordinates.
TEST(CoordinateManager, CoordinateManagerWormholeLogicalTranslatedTopLeft) {
    const size_t translated_x_start = 18;
    const size_t translated_y_start = 18;
    const CoreCoord expected_translated_coords =
        CoreCoord(translated_x_start, translated_y_start, CoreType::TENSIX, CoordSystem::TRANSLATED);

    const size_t max_num_harvested_y = 10;

    // We go up to numbers less than 2^10 - 1 to test all possible harvesting masks, we fon't want to try to convert
    // if everything is harvested.
    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y) - 1; harvesting_mask++) {
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});

        tt_xy_pair tensix_grid_size = wormhole::TENSIX_GRID_SIZE;

        size_t num_harvested_y = CoordinateManager::get_num_harvested(harvesting_mask);

        CoreCoord logical_coords = CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);
        CoreCoord noc0_coords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::NOC0);

        CoreCoord translated_from_logical =
            coordinate_manager->translate_coord_to(logical_coords, CoordSystem::TRANSLATED);
        CoreCoord translated_from_noc0 = coordinate_manager->translate_coord_to(noc0_coords, CoordSystem::TRANSLATED);

        EXPECT_EQ(translated_from_logical, expected_translated_coords);
        EXPECT_EQ(translated_from_noc0, expected_translated_coords);
    }
}

// Test that harvested noc0 coordinates map to the last row of the virtual coordinates.
TEST(CoordinateManager, CoordinateManagerWormholeNOC0TranslatedHarvestedMapping) {
    // Harvest first and second NOC layout row.
    const size_t harvesting_mask = (1 << 0) | (1 << 1);
    const size_t num_harvested = CoordinateManager::get_num_harvested(harvesting_mask);
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});

    const std::vector<tt_xy_pair> tensix_cores = wormhole::TENSIX_CORES_NOC0;
    const tt_xy_pair tensix_grid_size = wormhole::TENSIX_GRID_SIZE;

    const size_t translated_x_start = wormhole::tensix_translated_coordinate_start_x;
    const size_t translated_y_start = wormhole::tensix_translated_coordinate_start_y;

    size_t logical_x = 0;
    size_t logical_y = tensix_grid_size.y - num_harvested;

    for (size_t index = 0; index < num_harvested * tensix_grid_size.x; index++) {
        const CoreCoord noc0_core =
            CoreCoord(tensix_cores[index].x, tensix_cores[index].y, CoreType::TENSIX, CoordSystem::NOC0);
        const CoreCoord translated_core = coordinate_manager->translate_coord_to(noc0_core, CoordSystem::TRANSLATED);

        EXPECT_EQ(translated_core.x, translated_x_start + logical_x);
        EXPECT_EQ(translated_core.y, translated_y_start + logical_y);

        logical_x++;

        if (logical_x == tensix_grid_size.x) {
            logical_x = 0;
            logical_y++;
        }
    }
}

// Test translation of DRAM core coordinates. There is no DRAM harvesting on Wormhole,
// so logical coordinates should cover all noc0 coordinates.
TEST(CoordinateManager, CoordinateManagerWormholeDRAMNoHarvesting) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    const size_t num_dram_banks = wormhole::NUM_DRAM_BANKS;
    const size_t num_noc_ports_per_bank = wormhole::NUM_NOC_PORTS_PER_DRAM_BANK;
    const std::vector<tt_xy_pair>& dram_cores = flatten_vector(wormhole::DRAM_CORES_NOC0);

    for (size_t dram_bank = 0; dram_bank < num_dram_banks; dram_bank++) {
        for (size_t noc_port = 0; noc_port < num_noc_ports_per_bank; noc_port++) {
            const CoreCoord dram_logical(dram_bank, noc_port, CoreType::DRAM, CoordSystem::LOGICAL);
            const size_t noc0_core_index = dram_bank * num_noc_ports_per_bank + noc_port;
            const CoreCoord expected_noc0 = CoreCoord(
                dram_cores[noc0_core_index].x, dram_cores[noc0_core_index].y, CoreType::DRAM, CoordSystem::NOC0);

            const CoreCoord dram_noc0 = coordinate_manager->translate_coord_to(dram_logical, CoordSystem::NOC0);

            EXPECT_EQ(dram_noc0, expected_noc0);
        }
    }
}

// Test translation of logical to translated ethernet coordinates.
TEST(CoordinateManager, CoordinateManagerWormholeETHTranslated) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    // Check translation for all corners of eth cores.
    std::vector<std::pair<tt_xy_pair, tt_xy_pair>> input_output_eth_pairs = {
        {{1, 0}, {18, 16}}, {{9, 0}, {25, 16}}, {{1, 6}, {18, 17}}, {{9, 6}, {25, 17}}};

    for (auto& [input_pair, output_pair] : input_output_eth_pairs) {
        const CoreCoord eth_noc0 = CoreCoord(input_pair, CoreType::ETH, CoordSystem::NOC0);
        const CoreCoord eth_translated = coordinate_manager->translate_coord_to(eth_noc0, CoordSystem::TRANSLATED);
        EXPECT_EQ((tt_xy_pair)eth_translated, output_pair);
    }
}

// Test that noc0 and translated coordinates are the same for all logical coordinates.
TEST(CoordinateManager, CoordinateManagerWormholeARCTranslation) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);
    const tt_xy_pair arc_grid_size = wormhole::ARC_GRID_SIZE;

    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            const CoreCoord arc_logical = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);
            const CoreCoord arc_noc0 = coordinate_manager->translate_coord_to(arc_logical, CoordSystem::NOC0);
            const CoreCoord arc_translated =
                coordinate_manager->translate_coord_to(arc_logical, CoordSystem::TRANSLATED);

            EXPECT_EQ(arc_noc0.x, arc_translated.x);
            EXPECT_EQ(arc_noc0.y, arc_translated.y);
        }
    }
}

// Test that noc0 and translated coordinates are the same for all logical PCIE coordinates.
TEST(CoordinateManager, CoordinateManagerWormholePCIETranslation) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);
    const tt_xy_pair pcie_grid_size = wormhole::PCIE_GRID_SIZE;

    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            const CoreCoord pcie_logical = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);
            const CoreCoord pcie_noc0 = coordinate_manager->translate_coord_to(pcie_logical, CoordSystem::NOC0);
            const CoreCoord pcie_translated =
                coordinate_manager->translate_coord_to(pcie_logical, CoordSystem::TRANSLATED);

            EXPECT_EQ(pcie_noc0.x, pcie_translated.x);
            EXPECT_EQ(pcie_noc0.y, pcie_translated.y);
        }
    }
}

// Test that we assert properly if DRAM harvesting mask is non-zero for Wormhole.
TEST(CoordinateManager, CoordinateManagerWormholeDRAMHarvestingAssert) {
    EXPECT_THROW(CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {0, 1}), std::runtime_error);
}

// Test that we assert properly if ETH harvesting mask is non-zero for Wormhole.
TEST(CoordinateManager, CoordinateManagerWormholeETHHarvestingAssert) {
    EXPECT_THROW(
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {0, 0, 1}), std::runtime_error);
}

// Test that we properly get harvesting mask that is based on the noc0 layout of the chip.
TEST(CoordinateManager, CoordinateManagerWormholeNOC0LayoutTensixHarvestingMask) {
    const size_t max_num_harvested_y = 10;

    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {
        const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = harvesting_mask};
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, harvesting_masks);

        EXPECT_EQ(coordinate_manager->get_harvesting_masks().tensix_harvesting_mask, harvesting_mask);
    }
}

// Test whether we properly shuffle the harvesting mask based on the noc0 layout of the chip.
TEST(CoordinateManager, CoordinateManagerWormholeHarvestingShuffle) {
    for (size_t i = 0; i < wormhole::LOGICAL_HARVESTING_LAYOUT.size(); i++) {
        const size_t harvesting_mask_noc0_layout = (1 << wormhole::LOGICAL_HARVESTING_LAYOUT[i]);
        const size_t harvesting_mask =
            CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, harvesting_mask_noc0_layout);

        EXPECT_EQ(harvesting_mask, 1 << i);
    }
}

TEST(CoordinateManager, CoordinateManagerWormholeTranslationWithoutCoreType) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    EXPECT_EQ(
        coordinate_manager->translate_coord_to({0, 0}, CoordSystem::NOC0, CoordSystem::NOC0).core_type, CoreType::DRAM);
    EXPECT_EQ(
        coordinate_manager->translate_coord_to({0, 0}, CoordSystem::TRANSLATED, CoordSystem::NOC0).core_type,
        CoreType::DRAM);
    EXPECT_EQ(
        coordinate_manager->translate_coord_to({2, 2}, CoordSystem::NOC0, CoordSystem::NOC0).core_type,
        CoreType::TENSIX);
    // Not allowed for logical coord system.
    EXPECT_THROW(
        coordinate_manager->translate_coord_to({0, 0}, CoordSystem::LOGICAL, CoordSystem::NOC0), std::runtime_error);
    // Throws if nothing is located at this coordinate.
    EXPECT_THROW(
        coordinate_manager->translate_coord_to({100, 100}, CoordSystem::NOC0, CoordSystem::NOC0), std::runtime_error);
}

TEST(CoordinateManager, CoordinateManagerWormholeNoc1Noc0Mapping) {
    // clang-format off
    static const std::vector<tt_xy_pair> TENSIX_CORES_NOC1 = {
        {8, 10}, {7, 10}, {6, 10}, {5, 10}, {3, 10}, {2, 10}, {1, 10}, {0, 10},
        {8, 9},   {7, 9},  {6, 9},  {5, 9},  {3, 9},  {2, 9},  {1, 9},  {0, 9},
        {8, 8},   {7, 8},  {6, 8},  {5, 8},  {3, 8},  {2, 8},  {1, 8},  {0, 8},
        {8, 7},   {7, 7},  {6, 7},  {5, 7},  {3, 7},  {2, 7},  {1, 7},  {0, 7},
        {8, 6},   {7, 6},  {6, 6},  {5, 6},  {3, 6},  {2, 6},  {1, 6},  {0, 6},
        {8, 4},   {7, 4},  {6, 4},  {5, 4},  {3, 4},  {2, 4},  {1, 4},  {0, 4},
        {8, 3},   {7, 3},  {6, 3},  {5, 3},  {3, 3},  {2, 3},  {1, 3},  {0, 3},
        {8, 2},   {7, 2},  {6, 2},  {5, 2},  {3, 2},  {2, 2},  {1, 2},  {0, 2},
        {8, 1},   {7, 1},  {6, 1},  {5, 1},  {3, 1},  {2, 1},  {1, 1},  {0, 1},
        {8, 0},   {7, 0},  {6, 0},  {5, 0},  {3, 0},  {2, 0},  {1, 0},  {0, 0},
    };
    static const std::vector<std::vector<tt_xy_pair>> DRAM_CORES_NOC1 = {
        {{9, 11}, {9, 10}, {9, 0}},
        { {9, 6},  {9, 5}, {9, 4}},
        {{4, 11}, {4, 10}, {4, 0}},
        { {4, 9},  {4, 2}, {4, 1}},
        { {4, 8},  {4, 7}, {4, 3}},
        { {4, 6},  {4, 5}, {4, 4}}
    };
    static const std::vector<tt_xy_pair> ETH_CORES_NOC1 = {
       {{0, 11},
        {8, 11},
        {1, 11},
        {7, 11},
        {2, 11},
        {6, 11},
        {3, 11},
        {5, 11},
        {0, 5},
        {8, 5},
        {1, 5},
        {7, 5},
        {2, 5},
        {6, 5},
        {3, 5},
        {5, 5}}};
    static const std::vector<tt_xy_pair> ARC_CORES_NOC1 = {{9, 1}};
    static const std::vector<tt_xy_pair> PCIE_CORES_NOC1 = {{{9, 8}}};
    // clang-format on

    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    auto check_noc0_noc1_mapping = [coordinate_manager](
                                       const std::vector<tt_xy_pair>& noc0_cores,
                                       const std::vector<tt_xy_pair>& noc1_cores,
                                       const CoreType core_type) {
        for (uint32_t index = 0; index < noc0_cores.size(); index++) {
            const CoreCoord noc0_core =
                CoreCoord(noc0_cores[index].x, noc0_cores[index].y, core_type, CoordSystem::NOC0);
            const CoreCoord noc1_core = coordinate_manager->translate_coord_to(noc0_core, CoordSystem::NOC1);

            EXPECT_EQ(noc1_core.x, noc1_cores[index].x);
            EXPECT_EQ(noc1_core.y, noc1_cores[index].y);

            const CoreCoord noc0_core_from_noc1 = coordinate_manager->translate_coord_to(noc1_core, CoordSystem::NOC0);

            EXPECT_EQ(noc0_core_from_noc1.x, noc0_cores[index].x);
            EXPECT_EQ(noc0_core_from_noc1.y, noc0_cores[index].y);
        }
    };

    check_noc0_noc1_mapping(wormhole::TENSIX_CORES_NOC0, TENSIX_CORES_NOC1, CoreType::TENSIX);
    check_noc0_noc1_mapping(flatten_vector(wormhole::DRAM_CORES_NOC0), flatten_vector(DRAM_CORES_NOC1), CoreType::DRAM);
    check_noc0_noc1_mapping(wormhole::ETH_CORES_NOC0, ETH_CORES_NOC1, CoreType::ETH);
    check_noc0_noc1_mapping(wormhole::ARC_CORES_NOC0, ARC_CORES_NOC1, CoreType::ARC);
    check_noc0_noc1_mapping(wormhole::PCIE_CORES_NOC0, PCIE_CORES_NOC1, CoreType::PCIE);
}
