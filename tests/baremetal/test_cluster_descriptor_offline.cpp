// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <fstream>
#include <tt-logger/tt-logger.hpp>

#include "disjoint_set.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"

using namespace tt;
using namespace tt::umd;

int count_connections(
    const std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<ChipId, EthernetChannel>>>&
        connections) {
    size_t count = 0;
    for (const auto& [_, channels] : connections) {
        count += channels.size();
    }
    return count;
}

TEST(ApiClusterDescriptorOfflineTest, TestAllOfflineClusterDescriptors) {
    for (std::string cluster_desc_yaml : test_utils::GetAllClusterDescs()) {
        std::cout << "Testing " << cluster_desc_yaml << std::endl;
        std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(cluster_desc_yaml);

        std::unordered_set<ChipId> all_chips = cluster_desc->get_all_chips();
        std::unordered_map<ChipId, EthCoord> eth_chip_coords = cluster_desc->get_chip_locations();

        std::unordered_map<ChipId, std::unordered_set<ChipId>> chips_grouped_by_closest_mmio =
            cluster_desc->get_chips_grouped_by_closest_mmio();

        // Check that cluster_id is always the same for the same cluster.
        // Cluster id takes the value of the smallest chip_id in the cluster.
        for (auto const& [chip, coord] : eth_chip_coords) {
            if (cluster_desc_yaml != test_utils::GetClusterDescAbsPath("wormhole_2xN300_unconnected.yaml")) {
                EXPECT_EQ(coord.cluster_id, 0);
            } else {
                EXPECT_TRUE(coord.cluster_id == 0 || coord.cluster_id == 1);
            }
        }
    }
}

TEST(ApiClusterDescriptorOfflineTest, SeparateClusters) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("wormhole_2xN300_unconnected.yaml"));

    auto all_chips = cluster_desc->get_all_chips();
    DisjointSet<ChipId> chip_clusters;
    for (auto chip : all_chips) {
        chip_clusters.add_item(chip);
    }

    // Merge into clusters of chips.
    for (auto connection : cluster_desc->get_ethernet_connections()) {
        ChipId chip = connection.first;
        for (auto [channel, remote_chip_and_channel] : connection.second) {
            ChipId remote_chip = std::get<0>(remote_chip_and_channel);
            chip_clusters.merge(chip, remote_chip);
        }
    }

    // Print out the number of resulting clusters.
    std::cout << "Detected " << chip_clusters.get_num_sets() << " separate clusters." << std::endl;

    // Check that get_closes_mmio_capable_chip works.
    for (auto chip : all_chips) {
        ChipId closest_mmio_chip = cluster_desc->get_closest_mmio_capable_chip(chip);
        EXPECT_TRUE(chip_clusters.are_same_set(chip, closest_mmio_chip));
    }
}

TEST(ApiClusterDescriptorOfflineTest, ConstrainedTopology) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("wormhole_4xN300_mesh.yaml"));

    // Lambda which counts of unique chip links.
    auto count_unique_chip_connections =
        [](const std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<ChipId, EthernetChannel>>>&
               connections) {
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

TEST(ApiMockClusterTest, CreateMockClustersFromAllDescriptors) {
    for (const auto& descriptor_file : test_utils::GetAllClusterDescs()) {
        log_info(LogUMD, "Testing mock cluster creation from: {}", descriptor_file);
        std::unique_ptr<ClusterDescriptor> cluster_desc;
        ASSERT_NO_THROW(cluster_desc = ClusterDescriptor::create_from_yaml(descriptor_file))
            << "Failed to load cluster descriptor from: " << descriptor_file;

        ASSERT_NE(cluster_desc, nullptr) << "Cluster descriptor is null for: " << descriptor_file;
        ASSERT_FALSE(cluster_desc->get_all_chips().empty()) << "Cluster descriptor has no chips: " << descriptor_file;

        // This should return at least mmio chips in their own groups.
        EXPECT_GT(cluster_desc->get_chips_grouped_by_closest_mmio().size(), 0);

        std::unique_ptr<Cluster> mock_cluster_all;
        ASSERT_NO_THROW(
            mock_cluster_all = std::make_unique<Cluster>(
                ClusterOptions{.chip_type = ChipType::MOCK, .cluster_descriptor = cluster_desc.get()}))
            << "Failed to create mock cluster with all chips for: " << descriptor_file;

        ASSERT_NE(mock_cluster_all, nullptr) << "Mock cluster is null for: " << descriptor_file;

        // Writes and reads have no effect but we can check that the mock cluster is created successfully.
        std::vector<uint8_t> data(1024, 0);
        for (auto chip_id : mock_cluster_all->get_target_device_ids()) {
            CoreCoord any_tensix_core = mock_cluster_all->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)[0];
            mock_cluster_all->write_to_device(data.data(), data.size(), chip_id, any_tensix_core, 0);
            mock_cluster_all->read_from_device(data.data(), chip_id, any_tensix_core, 0, data.size());
        }
    }
}
