// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Chip specific API examples.

#include <gtest/gtest.h>

#include <memory>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

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
