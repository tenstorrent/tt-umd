// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include "umd/device/cluster.hpp"

using namespace tt::umd;

// This test is used that cluster can be created in a baremetal environment.
TEST(TestClusterBaremetal, BasicClusterAPI) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    EXPECT_EQ(cluster->get_target_device_ids().size(), 0);
    EXPECT_EQ(cluster->get_target_mmio_device_ids().size(), 0);
    EXPECT_EQ(cluster->get_target_remote_device_ids().size(), 0);
}
