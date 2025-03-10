/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gtest/gtest.h"
#include "umd/device/coordinate_manager.h"
#include "umd/device/wormhole_implementation.h"

using namespace tt::umd;
#include <iostream>

// Tests that all physical coordinates are same as all virtual coordinates
// when there is no harvesting.
TEST(CoordinateManager, CoordinateManagerWormholeNoHarvesting) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    // We expect full grid size since there is no harvesting.
    tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;
    for (size_t x = 0; x < tensix_grid_size.x; x++) {
        for (size_t y = 0; y < tensix_grid_size.y; y++) {
            const CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            const CoreCoord virtual_coords =
                coordinate_manager->translate_coord_to(logical_coords, CoordSystem::VIRTUAL);
            const CoreCoord physical_coords =
                coordinate_manager->translate_coord_to(logical_coords, CoordSystem::PHYSICAL);

            // Virtual and physical coordinates should be the same.
            EXPECT_EQ(physical_coords.x, virtual_coords.x);
            EXPECT_EQ(physical_coords.y, virtual_coords.y);
        }
    }
}

// Test basic translation to virtual and physical noc coordinates.
// We expect that the top left core will have virtual and physical coordinates (1, 1) and (1, 2) for
// the logical coordinates if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerWormholeTopLeftCore) {
    // This harvesting mask if targeting first row in NOC layout.
    const size_t harvesting_mask = (1 << 0);

    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});
    tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;

    CoreCoord logical_coords = CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);

    // Always expect same virtual coordinate for (0, 0) logical coordinate.
    CoreCoord virtual_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::VIRTUAL);
    EXPECT_EQ(virtual_cords, CoreCoord(1, 1, CoreType::TENSIX, CoordSystem::VIRTUAL));

    // This depends on harvesting mask. So expected physical coord is specific to this test and Wormhole arch.
    CoreCoord physical_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::PHYSICAL);
    EXPECT_EQ(physical_cords, CoreCoord(1, 2, CoreType::TENSIX, CoordSystem::PHYSICAL));
}

// Test basic translation to virtual and physical noc coordinates.
// We expect that the top right core will have virtual and physical coordinates (10, 1) and (10, 2) for
// the logical coordinates (9, 0) if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerWormholeTopRightCore) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, 1);

    tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;
    size_t max_x = tensix_grid_size.x - 1;
    EXPECT_EQ(max_x, 9);
    CoreCoord logical_coords = CoreCoord(max_x, 0, CoreType::TENSIX, CoordSystem::LOGICAL);

    // Always expect same virtual coordinate for (0, 0) logical coordinate.
    CoreCoord virtual_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::VIRTUAL);
    EXPECT_EQ(virtual_cords, CoreCoord(10, 1, CoreType::TENSIX, CoordSystem::VIRTUAL));

    // This depends on harvesting mask. So expected physical coord is specific to this test and Wormhole arch.
    CoreCoord physical_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::PHYSICAL);
    EXPECT_EQ(physical_cords, CoreCoord(10, 2, CoreType::TENSIX, CoordSystem::PHYSICAL));
}

// Test basic translation to virtual and physical noc coordinates.
// We expect that the bottom left core will have virtual and physical coordinates (1, 8) and (1, 9) for
// the logical coordinates (0, 6) if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerWormholeBottomLeftCore) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, 1);

    tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;
    size_t max_y = tensix_grid_size.y - 2;
    EXPECT_EQ(max_y, 6);
    CoreCoord logical_coords = CoreCoord(0, max_y, CoreType::TENSIX, CoordSystem::LOGICAL);

    // Always expect same virtual coordinate for (0, 0) logical coordinate.
    CoreCoord virtual_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::VIRTUAL);
    EXPECT_EQ(virtual_cords, CoreCoord(1, 8, CoreType::TENSIX, CoordSystem::VIRTUAL));

    // This depends on harvesting mask. So expected physical coord is specific to this test and Wormhole arch.
    CoreCoord physical_cords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::PHYSICAL);
    EXPECT_EQ(physical_cords, CoreCoord(1, 9, CoreType::TENSIX, CoordSystem::PHYSICAL));
}

// Test logical to physical coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of physical coordinates.
// For the reverse mapping back of physical to logical coordinates we expect that same logical coordinates are returned
// as from original mapping.
TEST(CoordinateManager, CoordinateManagerWormholeLogicalPhysicalMapping) {
    const size_t max_num_harvested_y = 10;

    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});

        std::map<CoreCoord, CoreCoord> logical_to_physical;
        std::set<CoreCoord> physical_coords_set;
        tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;

        size_t num_harvested_y = CoordinateManager::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
                CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
                CoreCoord physical_coords =
                    coordinate_manager->translate_coord_to(logical_coords, CoordSystem::PHYSICAL);
                logical_to_physical[logical_coords] = physical_coords;

                // Expect that logical to physical translation is 1-1 mapping. No duplicates for physical coordinates.
                EXPECT_EQ(physical_coords_set.count(physical_coords), 0);
                physical_coords_set.insert(physical_coords);
            }
        }

        // Expect that the number of physical coordinates is equal to the number of workers minus the number of
        // harvested rows.
        EXPECT_EQ(physical_coords_set.size(), tensix_grid_size.x * (tensix_grid_size.y - num_harvested_y));

        for (auto it : logical_to_physical) {
            CoreCoord physical_coords = it.second;
            CoreCoord logical_coords = coordinate_manager->translate_coord_to(physical_coords, CoordSystem::LOGICAL);

            // Expect that reverse mapping of physical coordinates gives the same logical coordinates
            // using which we got the physical coordinates.
            EXPECT_EQ(it.first, logical_coords);
        }
    }
}

// Test logical to virtual coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of virtual coordinates.
// For the reverse mapping back of virtual to logical coordinates we expect that same logical coordinates are returned
// as from original mapping.
TEST(CoordinateManager, CoordinateManagerWormholeLogicalVirtualMapping) {
    const size_t max_num_harvested_y = 10;

    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});

        std::map<CoreCoord, CoreCoord> logical_to_virtual;
        std::set<CoreCoord> virtual_coords_set;
        tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;

        size_t num_harvested_y = CoordinateManager::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
                CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
                CoreCoord virtual_coords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::VIRTUAL);
                logical_to_virtual[logical_coords] = virtual_coords;

                // Expect that logical to virtual translation is 1-1 mapping. No duplicates for virtual coordinates.
                EXPECT_EQ(virtual_coords_set.count(virtual_coords), 0);
                virtual_coords_set.insert(virtual_coords);
            }
        }

        for (auto it : logical_to_virtual) {
            CoreCoord virtual_coords = it.second;
            CoreCoord logical_coords = coordinate_manager->translate_coord_to(virtual_coords, CoordSystem::LOGICAL);

            // Expect that reverse mapping of virtual coordinates gives the same logical coordinates
            // using which we got the virtual coordinates.
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

        tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;

        size_t num_harvested_y = CoordinateManager::get_num_harvested(harvesting_mask);

        CoreCoord logical_coords = CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);
        CoreCoord physical_coords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::PHYSICAL);
        CoreCoord virtual_coords = coordinate_manager->translate_coord_to(logical_coords, CoordSystem::VIRTUAL);

        CoreCoord translated_from_logical =
            coordinate_manager->translate_coord_to(logical_coords, CoordSystem::TRANSLATED);
        CoreCoord translated_from_physical =
            coordinate_manager->translate_coord_to(physical_coords, CoordSystem::TRANSLATED);
        CoreCoord translated_from_virtual =
            coordinate_manager->translate_coord_to(virtual_coords, CoordSystem::TRANSLATED);

        EXPECT_EQ(translated_from_logical, expected_translated_coords);
        EXPECT_EQ(translated_from_physical, expected_translated_coords);
        EXPECT_EQ(translated_from_virtual, expected_translated_coords);
    }
}

// Test that harvested physical coordinates map to the last row of the virtual coordinates.
TEST(CoordinateManager, CoordinateManagerWormholePhysicalVirtualHarvestedMapping) {
    // Harvest first and second NOC layout row.
    const size_t harvesting_mask = (1 << 0) | (1 << 1);
    const size_t num_harvested = CoordinateManager::get_num_harvested(harvesting_mask);
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});

    const std::vector<tt_xy_pair> tensix_cores = tt::umd::wormhole::TENSIX_CORES;
    const tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;

    size_t virtual_index = (tensix_grid_size.y - num_harvested) * tensix_grid_size.x;

    for (size_t index = 0; index < num_harvested * tensix_grid_size.x; index++) {
        const CoreCoord physical_core =
            CoreCoord(tensix_cores[index].x, tensix_cores[index].y, CoreType::TENSIX, CoordSystem::PHYSICAL);
        const CoreCoord virtual_core = coordinate_manager->translate_coord_to(physical_core, CoordSystem::VIRTUAL);

        EXPECT_EQ(virtual_core.x, tensix_cores[virtual_index].x);
        EXPECT_EQ(virtual_core.y, tensix_cores[virtual_index].y);

        virtual_index++;
    }
}

// Test that harvested physical coordinates map to the last row of the virtual coordinates.
TEST(CoordinateManager, CoordinateManagerWormholePhysicalTranslatedHarvestedMapping) {
    // Harvest first and second NOC layout row.
    const size_t harvesting_mask = (1 << 0) | (1 << 1);
    const size_t num_harvested = CoordinateManager::get_num_harvested(harvesting_mask);
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, {harvesting_mask});

    const std::vector<tt_xy_pair> tensix_cores = tt::umd::wormhole::TENSIX_CORES;
    const tt_xy_pair tensix_grid_size = tt::umd::wormhole::TENSIX_GRID_SIZE;

    size_t virtual_index = (tensix_grid_size.y - num_harvested) * tensix_grid_size.x;

    const size_t translated_x_start = tt::umd::wormhole::tensix_translated_coordinate_start_x;
    const size_t translated_y_start = tt::umd::wormhole::tensix_translated_coordinate_start_y;

    size_t logical_x = 0;
    size_t logical_y = tensix_grid_size.y - num_harvested;

    for (size_t index = 0; index < num_harvested * tensix_grid_size.x; index++) {
        const CoreCoord physical_core =
            CoreCoord(tensix_cores[index].x, tensix_cores[index].y, CoreType::TENSIX, CoordSystem::PHYSICAL);
        const CoreCoord translated_core =
            coordinate_manager->translate_coord_to(physical_core, CoordSystem::TRANSLATED);

        const CoreCoord virtual_core = CoreCoord(
            tensix_cores[virtual_index].x, tensix_cores[virtual_index].y, CoreType::TENSIX, CoordSystem::VIRTUAL);
        const CoreCoord translated_core_from_virtual =
            coordinate_manager->translate_coord_to(virtual_core, CoordSystem::TRANSLATED);

        EXPECT_EQ(translated_core, translated_core_from_virtual);

        EXPECT_EQ(translated_core.x, translated_x_start + logical_x);
        EXPECT_EQ(translated_core.y, translated_y_start + logical_y);

        logical_x++;

        if (logical_x == tensix_grid_size.x) {
            logical_x = 0;
            logical_y++;
        }

        virtual_index++;
    }
}

// Test translation of DRAM core coordinates. There is no DRAM harvesting on Wormhole,
// so logical coordinates should cover all physical coordinates.
TEST(CoordinateManager, CoordinateManagerWormholeDRAMNoHarvesting) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    const size_t num_dram_banks = tt::umd::wormhole::NUM_DRAM_BANKS;
    const size_t num_noc_ports_per_bank = tt::umd::wormhole::NUM_NOC_PORTS_PER_DRAM_BANK;
    const std::vector<tt_xy_pair>& dram_cores = tt::umd::wormhole::DRAM_CORES;

    for (size_t dram_bank = 0; dram_bank < num_dram_banks; dram_bank++) {
        for (size_t noc_port = 0; noc_port < num_noc_ports_per_bank; noc_port++) {
            const CoreCoord dram_logical(dram_bank, noc_port, CoreType::DRAM, CoordSystem::LOGICAL);
            const size_t physical_core_index = dram_bank * num_noc_ports_per_bank + noc_port;
            const CoreCoord expected_physical = CoreCoord(
                dram_cores[physical_core_index].x,
                dram_cores[physical_core_index].y,
                CoreType::DRAM,
                CoordSystem::PHYSICAL);

            const CoreCoord dram_physical = coordinate_manager->translate_coord_to(dram_logical, CoordSystem::PHYSICAL);

            EXPECT_EQ(dram_physical, expected_physical);
        }
    }
}

// Test that physical and virtual coordinates are the same for all logical coordinates, since there is no DRAM
// harvesting.
TEST(CoordinateManager, CoordinateManagerWormholeETHPhysicalEqualVirtual) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);
    const size_t num_eth_channels = tt::umd::wormhole::NUM_ETH_CHANNELS;

    for (size_t eth_channel = 0; eth_channel < num_eth_channels; eth_channel++) {
        const CoreCoord eth_logical = CoreCoord(0, eth_channel, CoreType::ETH, CoordSystem::LOGICAL);
        const CoreCoord eth_virtual = coordinate_manager->translate_coord_to(eth_logical, CoordSystem::VIRTUAL);
        const CoreCoord eth_physical = coordinate_manager->translate_coord_to(eth_logical, CoordSystem::PHYSICAL);

        EXPECT_EQ(eth_virtual.x, eth_physical.x);
        EXPECT_EQ(eth_virtual.y, eth_physical.y);
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
        const CoreCoord eth_physical = CoreCoord(input_pair, CoreType::ETH, CoordSystem::PHYSICAL);
        const CoreCoord eth_translated = coordinate_manager->translate_coord_to(eth_physical, CoordSystem::TRANSLATED);
        EXPECT_EQ((tt_xy_pair)eth_translated, output_pair);
    }
}

// Test that virtual, physical and translated coordinates are the same for all logical coordinates.
TEST(CoordinateManager, CoordinateManagerWormholeARCTranslation) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);
    const tt_xy_pair arc_grid_size = tt::umd::wormhole::ARC_GRID_SIZE;

    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            const CoreCoord arc_logical = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);
            const CoreCoord arc_virtual = coordinate_manager->translate_coord_to(arc_logical, CoordSystem::VIRTUAL);
            const CoreCoord arc_physical = coordinate_manager->translate_coord_to(arc_logical, CoordSystem::PHYSICAL);
            const CoreCoord arc_translated =
                coordinate_manager->translate_coord_to(arc_logical, CoordSystem::TRANSLATED);

            EXPECT_EQ(arc_virtual.x, arc_physical.x);
            EXPECT_EQ(arc_virtual.y, arc_physical.y);

            EXPECT_EQ(arc_physical.x, arc_translated.x);
            EXPECT_EQ(arc_physical.y, arc_translated.y);
        }
    }
}

// Test that virtual, physical and translated coordinates are the same for all logical PCIE coordinates.
TEST(CoordinateManager, CoordinateManagerWormholePCIETranslation) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);
    const tt_xy_pair pcie_grid_size = tt::umd::wormhole::PCIE_GRID_SIZE;

    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            const CoreCoord pcie_logical = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);
            const CoreCoord pcie_virtual = coordinate_manager->translate_coord_to(pcie_logical, CoordSystem::VIRTUAL);
            const CoreCoord pcie_physical = coordinate_manager->translate_coord_to(pcie_logical, CoordSystem::PHYSICAL);
            const CoreCoord pcie_translated =
                coordinate_manager->translate_coord_to(pcie_logical, CoordSystem::TRANSLATED);

            EXPECT_EQ(pcie_virtual.x, pcie_physical.x);
            EXPECT_EQ(pcie_virtual.y, pcie_physical.y);

            EXPECT_EQ(pcie_virtual.x, pcie_translated.x);
            EXPECT_EQ(pcie_virtual.y, pcie_translated.y);
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

// Test that we properly get harvesting mask that is based on the physical layout of the chip.
TEST(CoordinateManager, CoordinateManagerWormholePhysicalLayoutTensixHarvestingMask) {
    const size_t max_num_harvested_y = 10;

    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {
        const HarvestingMasks harvesting_masks = {.tensix_harvesting_mask = harvesting_mask};
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true, harvesting_masks);

        EXPECT_EQ(coordinate_manager->get_harvesting_masks().tensix_harvesting_mask, harvesting_mask);
    }
}

// Test whether we properly shuffle the harvesting mask based on the physical layout of the chip.
TEST(CoordinateManager, CoordinateManagerWormholeHarvestingShuffle) {
    for (size_t i = 0; i < tt::umd::wormhole::LOGICAL_HARVESTING_LAYOUT.size(); i++) {
        const size_t harvesting_mask_physical_layout = (1 << tt::umd::wormhole::LOGICAL_HARVESTING_LAYOUT[i]);
        const size_t harvesting_mask =
            CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH::WORMHOLE_B0, harvesting_mask_physical_layout);

        EXPECT_EQ(harvesting_mask, 1 << i);
    }
}

TEST(CoordinateManager, CoordinateManagerWormholeTranslationWithoutCoreType) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::create_coordinate_manager(tt::ARCH::WORMHOLE_B0, true);

    EXPECT_EQ(
        coordinate_manager->translate_coord_to({0, 0}, CoordSystem::PHYSICAL, CoordSystem::PHYSICAL).core_type,
        CoreType::DRAM);
    EXPECT_EQ(
        coordinate_manager->translate_coord_to({0, 0}, CoordSystem::VIRTUAL, CoordSystem::PHYSICAL).core_type,
        CoreType::DRAM);
    EXPECT_EQ(
        coordinate_manager->translate_coord_to({2, 2}, CoordSystem::PHYSICAL, CoordSystem::PHYSICAL).core_type,
        CoreType::TENSIX);
    // Not allowed for logical coord system.
    EXPECT_THROW(
        coordinate_manager->translate_coord_to({0, 0}, CoordSystem::LOGICAL, CoordSystem::PHYSICAL),
        std::runtime_error);
    // Throws if nothing is located at this coordinate.
    EXPECT_THROW(
        coordinate_manager->translate_coord_to({100, 100}, CoordSystem::PHYSICAL, CoordSystem::PHYSICAL),
        std::runtime_error);
}
