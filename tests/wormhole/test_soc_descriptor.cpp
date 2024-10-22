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

TEST(SocDescWH, SocDesNoHarvesting) {
    tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), 0);
    tt_xy_pair worker_grid_size = soc_desc.worker_grid_size;
    for (size_t x = 0; x < worker_grid_size.x; x++) {
        for (size_t y = 0; y < worker_grid_size.y; y++) {
            tt_logical_coords logical_coords = tt_logical_coords(x, y);
            tt_virtual_coords virtual_coords = soc_desc.logical_to_virtual_coords(logical_coords);
            tt_physical_coords physical_coords = soc_desc.logical_to_physical_coords(logical_coords);
            EXPECT_EQ(physical_coords.x, virtual_coords.x);
            EXPECT_EQ(physical_coords.y, virtual_coords.y);
        }
    }
}

TEST(SocDescWH, SocDescSimpleHarvest) {
    for (size_t harvesting_mask = 0; harvesting_mask < 5; harvesting_mask++) {
        std::cout << "harvesting mask is " << harvesting_mask << std::endl;
        tt_SocDescriptor soc_desc = tt_SocDescriptor(test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml"), harvesting_mask);
    }
}