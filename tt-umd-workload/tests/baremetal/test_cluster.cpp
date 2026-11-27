// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

#include "tests/test_utils/device_test_utils.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd-workload/cluster_device_event_listener.hpp"

using namespace tt::umd;

// This test is used that cluster can be created in a baremetal environment.
TEST(TestClusterBaremetal, BasicClusterAPI) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    EXPECT_EQ(cluster->get_target_device_ids().size(), 0);
    EXPECT_EQ(cluster->get_target_mmio_device_ids().size(), 0);
    EXPECT_EQ(cluster->get_target_remote_device_ids().size(), 0);
}

TEST(TestClusterBaremetal, EventListenerReportsChipsWithoutEventSupport) {
    Cluster cluster(ClusterOptions{.chip_type = ChipType::MOCK, .target_devices = {0}});
    ASSERT_FALSE(cluster.get_target_device_ids().empty());

    std::set<ChipId> uncovered_chip_ids;
    std::unique_ptr<ClusterDeviceEventListener> listener = cluster.create_event_listener(&uncovered_chip_ids);

    // Mock chips have no device to read events from.
    ASSERT_NE(listener, nullptr);
    EXPECT_EQ(uncovered_chip_ids, cluster.get_target_device_ids());
    EXPECT_TRUE(listener->read_for(4, std::chrono::milliseconds(0)).empty());
}

TEST(TestClusterBaremetal, EventListenerReportsRequestedChipsWithoutEventSupport) {
    Cluster cluster(ClusterOptions{.chip_type = ChipType::MOCK, .target_devices = {0}});
    const ChipId chip_id = *cluster.get_target_device_ids().begin();

    std::set<ChipId> uncovered_chip_ids;
    std::unique_ptr<ClusterDeviceEventListener> listener =
        cluster.create_event_listener(std::set<ChipId>{chip_id}, &uncovered_chip_ids);

    ASSERT_NE(listener, nullptr);
    EXPECT_EQ(uncovered_chip_ids, std::set<ChipId>{chip_id});
}

TEST(TestClusterBaremetal, EventListenerRejectsEmptyChipSet) {
    Cluster cluster(ClusterOptions{.chip_type = ChipType::MOCK, .target_devices = {0}});

    EXPECT_THROW(cluster.create_event_listener({}), std::runtime_error);
}

TEST(TestClusterBaremetal, EventListenerRejectsChipNotInCluster) {
    Cluster cluster(ClusterOptions{.chip_type = ChipType::MOCK, .target_devices = {0}});
    const ChipId missing_chip_id = *cluster.get_target_device_ids().rbegin() + 1;

    EXPECT_THROW(cluster.create_event_listener(std::set<ChipId>{missing_chip_id}), std::runtime_error);
}

TEST(TestClusterBaremetal, EventListenerOutlivesItsCluster) {
    auto cluster = std::make_unique<Cluster>(ClusterOptions{.chip_type = ChipType::MOCK, .target_devices = {0}});
    std::unique_ptr<ClusterDeviceEventListener> listener = cluster->create_event_listener();

    cluster.reset();

    EXPECT_TRUE(listener->read_for(4, std::chrono::milliseconds(0)).empty());
}
