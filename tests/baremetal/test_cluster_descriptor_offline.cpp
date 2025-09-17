// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <fstream>

#include "disjoint_set.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"

using namespace tt::umd;

int count_connections(const std::unordered_map<
                      chip_id_t,
                      std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t>>>& connections) {
    size_t count = 0;
    for (const auto& [_, channels] : connections) {
        count += channels.size();
    }
    return count;
}

TEST(ApiClusterDescriptorOfflineTest, TestAllOfflineClusterDescriptors) {
    for (std::string cluster_desc_yaml : {
             "blackhole_P100.yaml",
             "blackhole_P150.yaml",
             "galaxy.yaml",
             "wormhole_2xN300_unconnected.yaml",
             "wormhole_4xN300_mesh.yaml",
             "wormhole_N150.yaml",
             "wormhole_N300.yaml",
             "wormhole_N300_routing_info.yaml",
             "wormhole_N300_board_info.yaml",
             "wormhole_N150_unique_ids.yaml",
             "wormhole_N300_with_remote_connections.yaml",
         }) {
        std::cout << "Testing " << cluster_desc_yaml << std::endl;
        std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(
            test_utils::GetAbsPath("tests/cluster_descriptor_examples/" + cluster_desc_yaml));

        std::unordered_set<chip_id_t> all_chips = cluster_desc->get_all_chips();
        std::unordered_map<chip_id_t, eth_coord_t> eth_chip_coords = cluster_desc->get_chip_locations();

        std::unordered_map<chip_id_t, std::unordered_set<chip_id_t>> chips_grouped_by_closest_mmio =
            cluster_desc->get_chips_grouped_by_closest_mmio();

        // Check that cluster_id is always the same for the same cluster.
        // Cluster id takes the value of the smallest chip_id in the cluster.
        for (auto const& [chip, coord] : eth_chip_coords) {
            if (cluster_desc_yaml != "wormhole_2xN300_unconnected.yaml") {
                EXPECT_EQ(coord.cluster_id, 0);
            } else {
                EXPECT_TRUE(coord.cluster_id == 0 || coord.cluster_id == 1);
            }
        }
    }
}

TEST(ApiClusterDescriptorOfflineTest, SeparateClusters) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(
        test_utils::GetAbsPath("tests/cluster_descriptor_examples/wormhole_2xN300_unconnected.yaml"));

    auto all_chips = cluster_desc->get_all_chips();
    DisjointSet<chip_id_t> chip_clusters;
    for (auto chip : all_chips) {
        chip_clusters.add_item(chip);
    }

    // Merge into clusters of chips.
    for (auto connection : cluster_desc->get_ethernet_connections()) {
        chip_id_t chip = connection.first;
        for (auto [channel, remote_chip_and_channel] : connection.second) {
            chip_id_t remote_chip = std::get<0>(remote_chip_and_channel);
            chip_clusters.merge(chip, remote_chip);
        }
    }

    // Print out the number of resulting clusters.
    std::cout << "Detected " << chip_clusters.get_num_sets() << " separate clusters." << std::endl;

    // Check that get_closes_mmio_capable_chip works.
    for (auto chip : all_chips) {
        chip_id_t closest_mmio_chip = cluster_desc->get_closest_mmio_capable_chip(chip);
        EXPECT_TRUE(chip_clusters.are_same_set(chip, closest_mmio_chip));
    }
}

TEST(ApiClusterDescriptorOfflineTest, ConstrainedTopology) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(
        test_utils::GetAbsPath("tests/cluster_descriptor_examples/wormhole_4xN300_mesh.yaml"));

    // Lambda which counts of unique chip links.
    auto count_unique_chip_connections =
        [](const std::unordered_map<
            chip_id_t,
            std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t>>>& connections) {
            std::unordered_set<int> unique_connections;
            for (const auto& [chip, channels] : connections) {
                for (const auto& [channel, remote_chip_and_channel] : channels) {
                    auto [remote_chip, remote_channel] = remote_chip_and_channel;
                    if (chip > remote_chip) {
                        // One int is calculated from two ints, so that we don't have to define a hash function for a
                        // pair<int, int>.
                        unique_connections.insert(chip * 1000 + remote_chip);
                    } else {
                        unique_connections.insert(remote_chip * 1000 + chip);
                    }
                }
            }
            return unique_connections.size();
        };

    // Check the original cluster descriptor, just so we know what we're starting with.
    EXPECT_EQ(cluster_desc->get_chips_with_mmio().size(), 4);
    EXPECT_EQ(cluster_desc->get_all_chips().size(), 8);
    EXPECT_EQ(count_connections(cluster_desc->get_ethernet_connections()), 40);
    EXPECT_EQ(count_unique_chip_connections(cluster_desc->get_ethernet_connections()), 10);
    EXPECT_EQ(cluster_desc->get_chips_grouped_by_closest_mmio().size(), 4);
    EXPECT_EQ(cluster_desc->get_chips_grouped_by_closest_mmio().at(0).size(), 2);
    EXPECT_EQ(cluster_desc->get_chips_grouped_by_closest_mmio().at(1).size(), 2);
    EXPECT_EQ(cluster_desc->get_chip_locations().size(), 8);

    // Create with just two PCI chips
    std::unique_ptr<ClusterDescriptor> constrained_cluster_desc =
        cluster_desc->create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1});

    EXPECT_EQ(constrained_cluster_desc->get_chips_with_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_all_chips().size(), 2);
    // There are two ethernet connections between the two chips, and each is reported 2 times
    EXPECT_EQ(count_connections(constrained_cluster_desc->get_ethernet_connections()), 4);
    // However we only have 2 chips that are connected, which is 1 edge.
    EXPECT_EQ(count_unique_chip_connections(constrained_cluster_desc->get_ethernet_connections()), 1);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(0).size(), 1);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(1).size(), 1);
    EXPECT_EQ(constrained_cluster_desc->get_chip_locations().size(), 2);
    // This is not serialized into yaml, but we'd expect it to also be constrained.
    // EXPECT_EQ(constrained_cluster_desc->get_chip_unique_ids().size(), 2);

    // Create with one card which is one PCI and one remote chip
    constrained_cluster_desc = cluster_desc->create_constrained_cluster_descriptor(cluster_desc.get(), {0, 4});

    EXPECT_EQ(constrained_cluster_desc->get_chips_with_mmio().size(), 1);
    EXPECT_EQ(constrained_cluster_desc->get_all_chips().size(), 2);
    EXPECT_EQ(count_connections(constrained_cluster_desc->get_ethernet_connections()), 4);
    EXPECT_EQ(count_unique_chip_connections(constrained_cluster_desc->get_ethernet_connections()), 1);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().size(), 1);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(0).size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chip_locations().size(), 2);

    // Create with two cards, 4 chips
    constrained_cluster_desc = cluster_desc->create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1, 4, 5});

    EXPECT_EQ(constrained_cluster_desc->get_chips_with_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_all_chips().size(), 4);
    EXPECT_EQ(count_connections(constrained_cluster_desc->get_ethernet_connections()), 16);
    EXPECT_EQ(count_unique_chip_connections(constrained_cluster_desc->get_ethernet_connections()), 4);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(0).size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(1).size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chip_locations().size(), 4);
}
