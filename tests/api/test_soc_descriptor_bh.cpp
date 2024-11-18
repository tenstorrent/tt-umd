/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "device/tt_soc_descriptor.h"
#include "gtest/gtest.h"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "tests/test_utils/soc_desc_test_utils.hpp"

// Blackhole workers - x-y annotation
// functional_workers:
//   [
//    1-2,   2-2,   3-2,   4-2,   5-2,   6-2,   7-2,   10-2,   11-2,   12-2,   13-2,   14-2,   15-2,   16-2,
//    1-3,   2-3,   3-3,   4-3,   5-3,   6-3,   7-3,   10-3,   11-3,   12-3,   13-3,   14-3,   15-3,   16-3,
//    1-4,   2-4,   3-4,   4-4,   5-4,   6-4,   7-4,   10-4,   11-4,   12-4,   13-4,   14-4,   15-4,   16-4,
//    1-5,   2-5,   3-5,   4-5,   5-5,   6-5,   7-5,   10-5,   11-5,   12-5,   13-5,   14-5,   15-5,   16-5,
//    1-6,   2-6,   3-6,   4-6,   5-6,   6-6,   7-6,   10-6,   11-6,   12-6,   13-6,   14-6,   15-6,   16-6,
//    1-7,   2-7,   3-7,   4-7,   5-7,   6-7,   7-7,   10-7,   11-7,   12-7,   13-7,   14-7,   15-7,   16-7,
//    1-8,   2-8,   3-8,   4-8,   5-8,   6-8,   7-8,   10-8,   11-8,   12-8,   13-8,   14-8,   15-8,   16-8,
//    1-9,   2-9,   3-9,   4-9,   5-9,   6-9,   7-9,   10-9,   11-9,   12-9,   13-9,   14-9,   15-9,   16-9,
//    1-10,  2-10,  3-10,  4-10,  5-10,  6-10,  7-10,  10-10,  11-10,  12-10,  13-10,  14-10,  15-10,  16-10,
//    1-11,  2-11,  3-11,  4-11,  5-11,  6-11,  7-11,  10-11,  11-11,  12-11,  13-11,  14-11,  15-11,  16-11,
//  ]

// Tests that all physical coordinates are same as all virtual coordinates
// when there is no harvesting.
TEST(SocDescriptor, SocDescriptorBHNoHarvesting) {
    tt_SocDescriptor soc_desc =
        tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml"), 0);

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
// We expect that the top left core will have virtual and physical coordinates (1, 2) and (2, 2) for
// the logical coordinates if the first row is harvested.
TEST(SocDescriptor, SocDescriptorBHTopLeftCore) {
    tt_SocDescriptor soc_desc =
        tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_no_eth.yaml"), 1);
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

    tt_logical_coords logical_coords = tt_logical_coords(0, 0);

    // Always expect same virtual coordinate for (0, 0) logical coordinate.
    tt_virtual_coords virtual_cords = soc_desc.to_virtual_coords(logical_coords);
    EXPECT_EQ(virtual_cords, tt_virtual_coords(1, 2));

    // This depends on harvesting mask. So expected physical coord is specific to this test and Blackhole arch.
    tt_physical_coords physical_cords = soc_desc.to_physical_coords(logical_coords);
    EXPECT_EQ(physical_cords, tt_physical_coords(2, 2));
}

// Test logical to physical coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of physical coordinates.
// For the reverse mapping back of physical to logical coordinates we expect that same logical coordinates are returned
// as from original mapping.
TEST(SocDescriptor, SocDescriptorBHLogicalPhysicalMapping) {
    const std::size_t max_num_harvested_x = 14;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch.yaml"));
    for (std::size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_x); harvesting_mask++) {
        soc_desc.perform_harvesting(harvesting_mask);

        std::map<tt_logical_coords, tt_physical_coords> logical_to_physical;
        std::set<tt_physical_coords> physical_coords_set;
        tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

        std::size_t num_harvested_x = test_utils::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < worker_grid_size.x - num_harvested_x; x++) {
            for (size_t y = 0; y < worker_grid_size.y; y++) {
                tt_logical_coords logical_coords = tt_logical_coords(x, y);
                tt_physical_coords physical_coords = soc_desc.to_physical_coords(logical_coords);
                logical_to_physical[logical_coords] = physical_coords;

                // Expect that logical to physical translation is 1-1 mapping. No duplicates for physical coordinates.
                EXPECT_EQ(physical_coords_set.count(physical_coords), 0);
                physical_coords_set.insert(physical_coords);
            }
        }

        EXPECT_EQ(physical_coords_set.size(), worker_grid_size.y * (worker_grid_size.x - num_harvested_x));

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
// For the reverse mapping back of virtual to logical coordinates we expect that same logical coordinates are returned
// as from original mapping.
TEST(SocDescriptor, SocDescriptorBHLogicalVirtualMapping) {
    const std::size_t max_num_harvested_x = 14;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch.yaml"));
    for (std::size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_x); harvesting_mask++) {
        soc_desc.perform_harvesting(harvesting_mask);

        std::map<tt_logical_coords, tt_virtual_coords> logical_to_virtual;
        std::set<tt_virtual_coords> virtual_coords_set;
        tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

        std::size_t num_harvested_x = test_utils::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < worker_grid_size.x - num_harvested_x; x++) {
            for (size_t y = 0; y < worker_grid_size.y; y++) {
                tt_logical_coords logical_coords = tt_logical_coords(x, y);
                tt_virtual_coords virtual_coords = soc_desc.to_virtual_coords(logical_coords);
                logical_to_virtual[logical_coords] = virtual_coords;

                // Expect that logical to virtual translation is 1-1 mapping. No duplicates for virtual coordinates.
                EXPECT_EQ(virtual_coords_set.count(virtual_coords), 0);
                virtual_coords_set.insert(virtual_coords);
            }
        }

        EXPECT_EQ(virtual_coords_set.size(), worker_grid_size.y * (worker_grid_size.x - num_harvested_x));

        for (auto it : logical_to_virtual) {
            tt_virtual_coords virtual_coords = it.second;
            tt_logical_coords logical_coords = soc_desc.to_logical_coords(virtual_coords);

            // Expect that reverse mapping of virtual coordinates gives the same logical coordinates
            // using which we got the virtual coordinates.
            EXPECT_EQ(it.first, logical_coords);
        }
    }
}

// Test logical to translated coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of translated coordinates.
// For the reverse mapping back of translated to logical coordinates we expect that same logical coordinates are
// returned as from original mapping.
TEST(SocDescriptor, SocDescriptorBHLogicalTranslatedMapping) {
    const std::size_t max_num_harvested_x = 14;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch.yaml"));
    for (std::size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_x); harvesting_mask++) {
        soc_desc.perform_harvesting(harvesting_mask);

        std::map<tt_logical_coords, tt_translated_coords> logical_to_translated;
        std::set<tt_translated_coords> translated_coords_set;
        tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

        std::size_t num_harvested_x = test_utils::get_num_harvested(harvesting_mask);

        for (size_t x = 0; x < worker_grid_size.x - num_harvested_x; x++) {
            for (size_t y = 0; y < worker_grid_size.y; y++) {
                tt_logical_coords logical_coords = tt_logical_coords(x, y);
                tt_translated_coords translated_coords = soc_desc.to_translated_coords(logical_coords);
                logical_to_translated[logical_coords] = translated_coords;

                // Expect that logical to translated translation is 1-1 mapping. No duplicates for translated
                // coordinates.
                EXPECT_EQ(translated_coords_set.count(translated_coords), 0);
                translated_coords_set.insert(translated_coords);
            }
        }

        EXPECT_EQ(translated_coords_set.size(), worker_grid_size.y * (worker_grid_size.x - num_harvested_x));

        for (auto it : logical_to_translated) {
            tt_translated_coords translated_coords = it.second;
            tt_logical_coords logical_coords = soc_desc.to_logical_coords(translated_coords);

            // Expect that reverse mapping of translated coordinates gives the same logical coordinates
            // using which we got the translated coordinates.
            EXPECT_EQ(it.first, logical_coords);
        }
    }
}

// Test that virtual and translated coordinates are same for all logical coordinates.
// This is expected for Blackhole way of harvesting.
TEST(SocDescriptor, SocDescriptorBHVirtualEqualTranslated) {
    const std::size_t max_num_harvested_x = 14;
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch.yaml"));
    for (std::size_t harvesting_mask = 0; harvesting_mask < (1 << max_num_harvested_x); harvesting_mask++) {
        soc_desc.perform_harvesting(harvesting_mask);

        std::size_t num_harvested_x = test_utils::get_num_harvested(harvesting_mask);

        for (std::size_t x = 0; x < soc_desc.worker_grid_size.x - num_harvested_x; x++) {
            for (std::size_t y = 0; y < soc_desc.worker_grid_size.y; y++) {
                tt_logical_coords logical_coords = tt_logical_coords(x, y);
                tt_translated_coords translated_coords = soc_desc.to_translated_coords(logical_coords);
                tt_virtual_coords virtual_coords = soc_desc.to_virtual_coords(logical_coords);

                // Expect that translated coordinates are same as virtual coordinates.
                EXPECT_EQ(translated_coords, virtual_coords);
            }
        }
    }
}
