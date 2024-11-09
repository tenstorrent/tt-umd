/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gtest/gtest.h"

#include "device/tt_soc_descriptor.h"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "tests/test_utils/soc_desc_test_utils.hpp"


// Wormhole workers - x-y annotation
// functional_workers:
//   [
//    1-1,   2-1,   3-1,   4-1,   6-1,   7-1,   8-1,   9-1, 
//    1-2,   2-2,   3-2,   4-2,   6-2,   7-2,   8-2,   9-2, 
//    1-3,   2-3,   3-3,   4-3,   6-3,   7-3,   8-3,   9-3, 
//    1-4,   2-4,   3-4,   4-4,   6-4,   7-4,   8-4,   9-4, 
//    1-5,   2-5,   3-5,   4-5,   6-5,   7-5,   8-5,   9-5, 
//    1-7,   2-7,   3-7,   4-7,   6-7,   7-7,   8-7,   9-7, 
//    1-8,   2-8,   3-8,   4-8,   6-8,   7-8,   8-8,   9-8, 
//    1-9,   2-9,   3-9,   4-9,   6-9,   7-9,   8-9,   9-9, 
//    1-10,  2-10,  3-10,  4-10,  6-10,  7-10,  8-10,  9-10, 
//    1-11,  2-11,  3-11,  4-11,  6-11,  7-11,  8-11,  9-11, 
//   ]

// Tests that all physical coordinates are same as all virtual coordinates
// when there is no harvesting.
TEST(SocDescriptor, SocDescriptorWHNoHarvesting) {

    const std::size_t harvesting_mask = 0;
    
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), harvesting_mask);

    // We expect full grid size since there is no harvesting.
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;
    for (size_t x = 0; x < worker_grid_size.x; x++) {
        for (size_t y = 0; y < worker_grid_size.y; y++) {
            tt_logical_coords logical_coords = tt_logical_coords(x, y);
            tt_virtual_coords virtual_coords = soc_desc.to_virtual_coords(logical_coords);
            tt_physical_coords physical_coords = soc_desc.to_physical_coords(logical_coords);

            // Virtual and physical coordinates should be the same.
            EXPECT_EQ(physical_coords, virtual_coords);
        }
    }
}

// Test basic translation to virtual and physical noc coordinates.
// We expect that the top left core will have virtual and physical coordinates (1, 1) and (1, 2) for
// the logical coordinates if the first row is harvested.
TEST(SocDescriptor, SocDescriptorWHTopLeftCore) {

    const std::size_t harvesting_mask = 1;

    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), harvesting_mask);
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

    tt_logical_coords logical_coords = tt_logical_coords(0, 0);

    // Always expect same virtual coordinate for (0, 0) logical coordinate.
    tt_virtual_coords virtual_cords = soc_desc.to_virtual_coords(logical_coords);
    EXPECT_EQ(virtual_cords, tt_virtual_coords(1, 1));

    // This depends on harvesting mask. So expected physical coord is specific to this test and Wormhole arch.
    tt_physical_coords physical_cords = soc_desc.to_physical_coords(logical_coords);
    EXPECT_EQ(physical_cords, tt_physical_coords(1, 2));
}

// Test logical to physical coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of physical coordinates.
// For the reverse mapping back of physical to logical coordinates we expect that same logical coordinates are returned as from original mapping.
TEST(SocDescriptor, SocDescriptorWHLogicalPhysicalMapping) {

    const std::size_t max_num_harvested_y = 10;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"));
    for (std::size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {

        soc_desc.perform_harvesting(harvesting_mask);

        std::map<tt_logical_coords, tt_physical_coords> logical_to_physical;
        std::set<tt_physical_coords> physical_coords_set;
        tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

        std::size_t num_harvested_y = test_utils::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < worker_grid_size.x; x++) {
            for (size_t y = 0; y < worker_grid_size.y - num_harvested_y; y++) {
                tt_logical_coords logical_coords = tt_logical_coords(x, y);
                tt_physical_coords physical_coords = soc_desc.to_physical_coords(logical_coords);
                logical_to_physical[logical_coords] = physical_coords;

                // Expect that logical to physical translation is 1-1 mapping. No duplicates for physical coordinates.
                EXPECT_EQ(physical_coords_set.count(physical_coords), 0);
                physical_coords_set.insert(physical_coords);
            }
        }
        
        // Expect that the number of physical coordinates is equal to the number of workers minus the number of harvested rows.
        EXPECT_EQ(physical_coords_set.size(), worker_grid_size.x * (worker_grid_size.y - num_harvested_y));

        for (auto it : logical_to_physical) {
            tt_physical_coords physical_coords = it.second;
            tt_logical_coords logical_coords = soc_desc.to_logical_coords(physical_coords);

            // Expect that reverse mapping of physical coordinates gives the same logical coordinates
            // using which we got the physical coordinates.
            EXPECT_EQ(it.first, logical_coords);
        }
    }
}

// Test logical to virtual coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of virtual coordinates.
// For the reverse mapping back of virtual to logical coordinates we expect that same logical coordinates are returned as from original mapping.
TEST(SocDescriptor, SocDescriptorWHLogicalVirtualMapping) {

    const std::size_t max_num_harvested_y = 10;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"));
    for (std::size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y); harvesting_mask++) {

        soc_desc.perform_harvesting(harvesting_mask);

        std::map<tt_logical_coords, tt_virtual_coords> logical_to_virtual;
        std::set<tt_virtual_coords> virtual_coords_set;
        tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

        std::size_t num_harvested_y = test_utils::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < worker_grid_size.x; x++) {
            for (size_t y = 0; y < worker_grid_size.y - num_harvested_y; y++) {
                tt_logical_coords logical_coords = tt_logical_coords(x, y);
                tt_virtual_coords virtual_coords = soc_desc.to_virtual_coords(logical_coords);
                logical_to_virtual[logical_coords] = virtual_coords;

                // Expect that logical to virtual translation is 1-1 mapping. No duplicates for virtual coordinates.
                EXPECT_EQ(virtual_coords_set.count(virtual_coords), 0);
                virtual_coords_set.insert(virtual_coords);
            }
        }

        for (auto it : logical_to_virtual) {
            tt_virtual_coords virtual_coords = it.second;
            tt_logical_coords logical_coords = soc_desc.to_logical_coords(virtual_coords);

            // Expect that reverse mapping of virtual coordinates gives the same logical coordinates
            // using which we got the virtual coordinates.
            EXPECT_EQ(it.first, logical_coords);
        }
    }
}

// Test top left corner translation from logical to translated coordinates.
TEST(SocDescriptor, SocDescriptorWHLogicalTranslatedTopLeft) {

    const std::size_t translated_x_start = 18;
    const std::size_t translated_y_start = 18;
    const tt_translated_coords expected_translated_coords = tt_translated_coords(translated_x_start, translated_y_start);

    const std::size_t max_num_harvested_y = 10;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"));
    // We go up to numbers less than 2^10 - 1 to test all possible harvesting masks, we don't want to try to convert if everything is harvested.
    for (std::size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_y) - 1; harvesting_mask++) {
        soc_desc.perform_harvesting(harvesting_mask);
        
        tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

        std::size_t num_harvested_y = test_utils::get_num_harvested(harvesting_mask);

        tt_logical_coords logical_coords = tt_logical_coords(0, 0);
        tt_physical_coords physical_coords = soc_desc.to_physical_coords(logical_coords);
        tt_virtual_coords virtual_coords = soc_desc.to_virtual_coords(logical_coords);

        tt_translated_coords translated_from_logical = soc_desc.to_translated_coords(logical_coords);
        tt_translated_coords translated_from_physical = soc_desc.to_translated_coords(physical_coords);
        tt_translated_coords translated_from_virtual = soc_desc.to_translated_coords(virtual_coords);

        EXPECT_EQ(translated_from_logical, expected_translated_coords);
        EXPECT_EQ(translated_from_physical, expected_translated_coords);
        EXPECT_EQ(translated_from_virtual, expected_translated_coords);
    }
}
