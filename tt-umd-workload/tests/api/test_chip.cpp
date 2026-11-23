// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Chip specific API examples.

#include <gtest/gtest.h>

#include <memory>

#include "tests/test_utils/device_test_utils.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

using namespace tt;
using namespace tt::umd;

// TODO: Move to test_chip.
TEST(ApiChipTest, SimpleAPIShowcase) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();

    ChipId chip_id = umd_cluster->get_cluster_description()->get_chips_with_mmio().begin()->first;

    // TODO: In future, will be accessed through Chip api.
    umd_cluster->get_sysmem_window_noc_base(chip_id);
    umd_cluster->get_num_host_channels(chip_id);
}

// A remote chip reaches the host only through its gateway chip, so it has no host memory of its own to hand out.
TEST(ApiChipTest, RemoteChipHasNoSystemMemoryAllocator) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No remote chips found.";
    }

    for (ChipId chip_id : cluster->get_target_remote_device_ids()) {
        EXPECT_EQ(cluster->get_chip(chip_id)->get_system_memory_allocator(), nullptr) << "chip " << chip_id;
    }
}
