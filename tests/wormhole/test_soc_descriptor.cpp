#include <thread>
#include <memory>
#include <random>

#include "gtest/gtest.h"
#include "tt_device.h"
#include "eth_l1_address_map.h"
#include "l1_address_map.h"
#include "eth_l1_address_map.h"
#include "eth_interface.h"
#include "host_mem_address_map.h"

#include "device/tt_cluster_descriptor.h"
#include "device/wormhole/wormhole_implementation.h"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "tests/test_utils/device_test_utils.hpp"

// eth:
//  [Translated x: 18   19   20   21   22   23   24   25                                            Translated y
//                 1-0, 2-0, 3-0, 4-0, 6-0, 7-0, 8-0, 9-0,                                               16
//                 1-6, 2-6, 3-6, 4-6, 6-6, 7-6, 8-6, 9-6,                                               17 
//  ]
// functional_workers:
//  [Translated x: 18     19     20     21     22     23     24     25
//                                                                                                  Translated y
//  0               1-1,   2-1,   3-1,   4-1,   6-1,   7-1,   8-1,   9-1, # Row 1                         18
//  1               1-2,   2-2,   3-2,   4-2,   6-2,   7-2,   8-2,   9-2, # Row 2                         19
//  2               1-3,   2-3,   3-3,   4-3,   6-3,   7-3,   8-3,   9-3, # Row 3                         20
//  3               1-4,   2-4,   3-4,   4-4,   6-4,   7-4,   8-4,   9-4, # Harvested row 4
//  4               1-5,   2-5,   3-5,   4-5,   6-5,   7-5,   8-5,   9-5, # Row 5                         21
//  5               1-7,   2-7,   3-7,   4-7,   6-7,   7-7,   8-7,   9-7, # Row 6                         22
//  6               1-8,   2-8,   3-8,   4-8,   6-8,   7-8,   8-8,   9-8, # Row 7                         23
//  7               1-9,   2-9,   3-9,   4-9,   6-9,   7-9,   8-9,   9-9, # Harvested row 8   
//  8               1-10,  2-10,  3-10,  4-10,  6-10,  7-10,  8-10,  9-10, # Row 9                        24
//  9               1-11,  2-11,  3-11,  4-11,  6-11,  7-11,  8-11,  9-11, # Row 10                       25
//  ]

// Tests that all physical coordinates are same as all virtual coordinates
// when there is no harvesting.
TEST(SocDescriptorWH, SocDescriptorNoHarvesting) {

    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), 0);

    // We expect full grid size since there is no harvesting.
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;
    for (size_t x = 0; x < worker_grid_size.x; x++) {
        for (size_t y = 0; y < worker_grid_size.y; y++) {
            tt_logical_coords logical_coords = tt_logical_coords(x, y);
            tt_virtual_coords virtual_coords = soc_desc.logical_to_virtual_coords(logical_coords);
            tt_physical_coords physical_coords = soc_desc.logical_to_physical_coords(logical_coords);
            EXPECT_EQ(physical_coords, virtual_coords);
        }
    }
}

// Test basic translation to virtual and physical noc coordinates.
// We expect that the top left core will have virtual and physical coordinates (1, 1) and (1, 2) for
// the logical coordinates if the first row is harvested.
TEST(SocDescriptorWH, SocDescriptorTopLeftCore) {
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), 1);
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

    tt_logical_coords logical_coords = tt_logical_coords(0, 0);

    tt_virtual_coords virtual_cords = soc_desc.logical_to_virtual_coords(logical_coords);
    EXPECT_EQ(virtual_cords, tt_virtual_coords(1, 1));

    tt_physical_coords physical_cords = soc_desc.logical_to_physical_coords(logical_coords);
    EXPECT_EQ(physical_cords, tt_physical_coords(1, 2));
}

// Test logical to physical coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of physical coordinates.
// For the reverse mapping back of physical to logical coordinates we expect that same logical coordinates are returned as from original mapping.
TEST(SocDescriptorWH, SocDescriptorLogicalPhysicalMapping) {
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), 5);

    std::map<tt_logical_coords, tt_physical_coords> logical_to_physical;
    std::set<tt_physical_coords> physical_coords_set;
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

    std::size_t num_harvested_y = 2;

    for (size_t x = 0; x < worker_grid_size.x; x++) {
        for (size_t y = 0; y < worker_grid_size.y - num_harvested_y; y++) {
            tt_logical_coords logical_coords = tt_logical_coords(x, y);
            tt_physical_coords physical_coords = soc_desc.logical_to_physical_coords(logical_coords);
            logical_to_physical[logical_coords] = physical_coords;

            EXPECT_EQ(physical_coords_set.count(physical_coords), 0);
            physical_coords_set.insert(physical_coords);
        }
    }

    for (auto it : logical_to_physical) {
        tt_physical_coords physical_coords = it.second;
        tt_logical_coords logical_coords = soc_desc.physical_to_logical_coords(physical_coords);
        EXPECT_EQ(it.first, logical_coords);
    }
}

// Test logical to virtual coordinate translation.
// For the full grid of logical coordinates we expect that there are no duplicates of virtual coordinates.
// For the reverse mapping back of virtual to logical coordinates we expect that same logical coordinates are returned as from original mapping.
TEST(SocDescriptorWH, SocDescriptorLogicalVirtualMapping) {
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), 3);

    std::map<tt_logical_coords, tt_virtual_coords> logical_to_virtual;
    std::set<tt_virtual_coords> virtual_coords_set;
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;

    std::size_t num_harvested_y = 2;

    for (size_t x = 0; x < worker_grid_size.x; x++) {
        for (size_t y = 0; y < worker_grid_size.y - num_harvested_y; y++) {
            tt_logical_coords logical_coords = tt_logical_coords(x, y);
            tt_virtual_coords virtual_coords = soc_desc.logical_to_virtual_coords(logical_coords);
            logical_to_virtual[logical_coords] = virtual_coords;

            EXPECT_EQ(virtual_coords_set.count(virtual_coords), 0);
            virtual_coords_set.insert(virtual_coords);
        }
    }

    for (auto it : logical_to_virtual) {
        tt_virtual_coords virtual_coords = it.second;
        tt_logical_coords logical_coords = soc_desc.virtual_to_logical_coords(virtual_coords);
        EXPECT_EQ(it.first, logical_coords);
    }
}
