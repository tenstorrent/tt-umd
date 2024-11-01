/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gtest/gtest.h"
#include "tests/test_utils/soc_desc_test_utils.hpp"
#include "umd/device/coordinate_manager.h"
#include "umd/device/grayskull_implementation.h"

using namespace tt::umd;

// Tests that all physical coordinates are same as all virtual coordinates
// when there is no harvesting.
TEST(CoordinateManager, CoordinateManagerGrayskullNoHarvesting) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL);

    // We expect full grid size since there is no harvesting.
    tt_xy_pair tensix_grid_size = tt::umd::grayskull::TENSIX_GRID_SIZE;
    for (size_t x = 0; x < tensix_grid_size.x; x++) {
        for (size_t y = 0; y < tensix_grid_size.y; y++) {
            CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            CoreCoord virtual_coords = coordinate_manager->to(logical_coords, CoordSystem::VIRTUAL);
            CoreCoord physical_coords = coordinate_manager->to(logical_coords, CoordSystem::PHYSICAL);

            // Virtual and physical coordinates should be the same.
            EXPECT_EQ(physical_coords.x, virtual_coords.x);
            EXPECT_EQ(physical_coords.y, virtual_coords.y);
        }
    }
}

// Test basic translation to virtual and physical noc coordinates.
// We expect that the top left core will have virtual and physical coordinates (1, 1) and (1, 2) for
// the logical coordinates if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerGrayskullTopLeftCore) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL);

    CoreCoord logical_coords = CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);

    // Always expect same virtual coordinate for (0, 0) logical coordinate.
    CoreCoord virtual_cords = coordinate_manager->to(logical_coords, CoordSystem::VIRTUAL);
    EXPECT_EQ(virtual_cords, CoreCoord(1, 1, CoreType::TENSIX, CoordSystem::VIRTUAL));

    // This depends on harvesting mask. So expected physical coord is specific to this test and Wormhole arch.
    CoreCoord physical_cords = coordinate_manager->to(logical_coords, CoordSystem::PHYSICAL);
    EXPECT_EQ(physical_cords, CoreCoord(1, 1, CoreType::TENSIX, CoordSystem::PHYSICAL));
}

// Test basic translation to virtual and physical noc coordinates with harvesting.
// We expect that the top left core will have virtual and physical coordinates (1, 1) and (1, 2) for
// the logical coordinates if the first row is harvested.
TEST(CoordinateManager, CoordinateManagerGrayskullTopLeftCoreHarvesting) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL, 1);

    CoreCoord logical_coords = CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);

    // Always expect same virtual coordinate for (0, 0) logical coordinate.
    CoreCoord virtual_cords = coordinate_manager->to(logical_coords, CoordSystem::VIRTUAL);
    EXPECT_EQ(virtual_cords, CoreCoord(1, 1, CoreType::TENSIX, CoordSystem::VIRTUAL));

    // This depends on harvesting mask. So expected physical coord is specific to this test and Wormhole arch.
    CoreCoord physical_cords = coordinate_manager->to(logical_coords, CoordSystem::PHYSICAL);
    EXPECT_EQ(physical_cords, CoreCoord(1, 2, CoreType::TENSIX, CoordSystem::PHYSICAL));
}

// Test logical to physical, virtual and translated coordinates.
// We always expect that physical, virtual and translated coordinates are the same.
TEST(CoordinateManager, CoordinateManagerGrayskullTranslatingCoords) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL);
    tt_xy_pair tensix_grid_size = tt::umd::grayskull::TENSIX_GRID_SIZE;

    for (size_t x = 0; x < tensix_grid_size.x; x++) {
        for (size_t y = 0; y < tensix_grid_size.y; y++) {
            CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            CoreCoord virtual_coords = coordinate_manager->to(logical_coords, CoordSystem::VIRTUAL);
            CoreCoord physical_coords = coordinate_manager->to(logical_coords, CoordSystem::PHYSICAL);
            CoreCoord translated_coords = coordinate_manager->to(logical_coords, CoordSystem::TRANSLATED);

            // Virtual, physical and translated coordinates should be the same.
            EXPECT_EQ(physical_coords.x, virtual_coords.x);
            EXPECT_EQ(physical_coords.y, virtual_coords.y);

            EXPECT_EQ(physical_coords.x, translated_coords.x);
            EXPECT_EQ(physical_coords.y, translated_coords.y);
        }
    }
}

// Test logical to physical coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of physical coordinates.
// For the reverse mapping back of physical to logical coordinates we expect that same logical coordinates are returned
// as from original mapping.
TEST(CoordinateManager, CoordinateManagerGrayskullLogicalPhysicalMapping) {
    const size_t max_num_harvested_y = 10;
    const tt_xy_pair tensix_grid_size = tt::umd::grayskull::TENSIX_GRID_SIZE;

    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL, harvesting_mask);

        std::map<CoreCoord, CoreCoord> logical_to_physical;
        std::set<CoreCoord> physical_coords_set;

        size_t num_harvested_y = test_utils::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
                CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
                CoreCoord physical_coords = coordinate_manager->to(logical_coords, CoordSystem::PHYSICAL);
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
            CoreCoord logical_coords = coordinate_manager->to(physical_coords, CoordSystem::LOGICAL);

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
TEST(CoordinateManager, CoordinateManagerGrayskullLogicalVirtualMapping) {
    const size_t max_num_harvested_y = 10;
    const tt_xy_pair tensix_grid_size = tt::umd::grayskull::TENSIX_GRID_SIZE;

    for (size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {
        std::shared_ptr<CoordinateManager> coordinate_manager =
            CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL, harvesting_mask);

        std::map<CoreCoord, CoreCoord> logical_to_virtual;
        std::set<CoreCoord> virtual_coords_set;

        size_t num_harvested_y = test_utils::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
                CoreCoord logical_coords = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
                CoreCoord virtual_coords = coordinate_manager->to(logical_coords, CoordSystem::VIRTUAL);
                logical_to_virtual[logical_coords] = virtual_coords;

                // Expect that logical to virtual translation is 1-1 mapping. No duplicates for virtual coordinates.
                EXPECT_EQ(virtual_coords_set.count(virtual_coords), 0);
                virtual_coords_set.insert(virtual_coords);
            }
        }

        for (auto it : logical_to_virtual) {
            CoreCoord virtual_coords = it.second;
            CoreCoord logical_coords = coordinate_manager->to(virtual_coords, CoordSystem::LOGICAL);

            // Expect that reverse mapping of virtual coordinates gives the same logical coordinates
            // using which we got the virtual coordinates.
            EXPECT_EQ(it.first, logical_coords);
        }
    }
}

// Test mapping of DRAM coordinates from logical to physical. We have no DRAM harvesting on Grayskull,
// so logical coordinates should cover all physical coordinates.
TEST(CoordinateManager, CoordinateManagerGrayskullDRAMNoHarvesting) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL);

    const size_t num_dram_banks = tt::umd::grayskull::NUM_DRAM_BANKS;
    const std::vector<tt_xy_pair>& dram_cores = tt::umd::grayskull::DRAM_CORES;

    for (size_t dram_bank = 0; dram_bank < num_dram_banks; dram_bank++) {
        const CoreCoord dram_logical(dram_bank, 0, CoreType::DRAM, CoordSystem::LOGICAL);
        const CoreCoord expected_physical =
            CoreCoord(dram_cores[dram_bank].x, dram_cores[dram_bank].y, CoreType::DRAM, CoordSystem::PHYSICAL);

        const CoreCoord dram_physical = coordinate_manager->to(dram_logical, CoordSystem::PHYSICAL);

        EXPECT_EQ(dram_physical, expected_physical);
    }
}

// Test that virtual, physical and translated coordinates are the same for all logical PCIE coordinates.
TEST(CoordinateManager, CoordinateManagerGrayskullPCIETranslation) {
    std::shared_ptr<CoordinateManager> coordinate_manager =
        CoordinateManager::get_coordinate_manager(tt::ARCH::GRAYSKULL);
    const tt_xy_pair pcie_grid_size = tt::umd::grayskull::PCIE_GRID_SIZE;

    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            const CoreCoord arc_logical = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);
            const CoreCoord arc_virtual = coordinate_manager->to(arc_logical, CoordSystem::VIRTUAL);
            const CoreCoord arc_physical = coordinate_manager->to(arc_logical, CoordSystem::PHYSICAL);
            const CoreCoord arc_translated = coordinate_manager->to(arc_logical, CoordSystem::TRANSLATED);

            EXPECT_EQ(arc_virtual.x, arc_physical.x);
            EXPECT_EQ(arc_virtual.y, arc_physical.y);

            EXPECT_EQ(arc_physical.x, arc_translated.x);
            EXPECT_EQ(arc_physical.y, arc_translated.y);
        }
    }
}
