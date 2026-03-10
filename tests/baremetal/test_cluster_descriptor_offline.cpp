// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "disjoint_set.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "utils.hpp"

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
    for (const std::string& cluster_desc_yaml : test_utils::GetAllClusterDescs()) {
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
                EXPECT_TRUE(coord.cluster_id == 0 || coord.cluster_id == 2);
            }
        }
    }
}

TEST(ApiClusterDescriptorOfflineTest, TestAllOfflineClusterDescriptorsContent) {
    for (const std::string& cluster_desc_yaml : test_utils::GetAllClusterDescs()) {
        std::cout << "Testing " << cluster_desc_yaml << std::endl;

        // Load file content.
        std::ifstream fdesc(cluster_desc_yaml);
        EXPECT_FALSE(fdesc.fail());
        std::stringstream buffer;
        buffer << fdesc.rdbuf();
        fdesc.close();
        std::string file_content = buffer.str();

        std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml_content(file_content);

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
                EXPECT_TRUE(coord.cluster_id == 0 || coord.cluster_id == 2);
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
    for (const auto& connection : cluster_desc->get_ethernet_connections()) {
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
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("t3k_cluster_desc.yaml"));

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

    // Create with just two PCI chips.
    std::unique_ptr<ClusterDescriptor> constrained_cluster_desc =
        cluster_desc->create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1});

    EXPECT_EQ(constrained_cluster_desc->get_chips_with_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_all_chips().size(), 4);
    // There are two ethernet connections between the two chips, and each is reported 2 times.
    EXPECT_EQ(count_connections(constrained_cluster_desc->get_ethernet_connections()), 16);
    // However we only have 2 chips that are connected, which is 1 edge.
    EXPECT_EQ(count_unique_chip_connections(constrained_cluster_desc->get_ethernet_connections()), 4);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(0).size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(1).size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chip_locations().size(), 4);
    // This is not serialized into yaml, but we'd expect it to also be constrained.
    EXPECT_EQ(constrained_cluster_desc->get_chip_unique_ids().size(), 4);
    if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
        ASSERT_TRUE(false) << "Failed to unset TT_VISIBLE_DEVICES environment variable.";
    }
}

TEST(ApiClusterDescriptorOfflineTest, ConstrainedTopologyTTVisibleDevices) {
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("t3k_cluster_desc.yaml"));

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

    // Create with just two PCI chips.
    std::unique_ptr<ClusterDescriptor> constrained_cluster_desc =
        cluster_desc->create_constrained_cluster_descriptor(cluster_desc.get(), {0, 1});

    EXPECT_EQ(constrained_cluster_desc->get_chips_with_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_all_chips().size(), 4);
    // There are two ethernet connections between the two chips, and each is reported 2 times.
    EXPECT_EQ(count_connections(constrained_cluster_desc->get_ethernet_connections()), 16);
    // However we only have 2 chips that are connected, which is 1 edge.
    EXPECT_EQ(count_unique_chip_connections(constrained_cluster_desc->get_ethernet_connections()), 4);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(0).size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chips_grouped_by_closest_mmio().at(1).size(), 2);
    EXPECT_EQ(constrained_cluster_desc->get_chip_locations().size(), 4);
    // This is not serialized into yaml, but we'd expect it to also be constrained.
    EXPECT_EQ(constrained_cluster_desc->get_chip_unique_ids().size(), 4);
}

TEST(ApiClusterDescriptorOfflineTest, NoBoardExpansion) {
    // Load the 6u cluster descriptor (Galaxy-style with many chips per board).
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("6u_cluster_desc.yaml"));
    ASSERT_NE(cluster_desc, nullptr) << "Failed to load cluster descriptor";

    // Test 1: With explicit target_chip_ids, should NOT expand to include all chips on the same boards.
    std::unordered_set<ChipId> target_chips = {0, 1, 2, 3};
    std::unique_ptr<ClusterDescriptor> constrained_desc =
        ClusterDescriptor::create_constrained_cluster_descriptor(cluster_desc.get(), target_chips);

    ASSERT_NE(constrained_desc, nullptr);
    std::unordered_set<ChipId> constrained_chips = constrained_desc->get_all_chips();

    // Should have exactly the specified chips, not all chips on the same board.
    EXPECT_EQ(constrained_chips.size(), target_chips.size())
        << "Should have exactly " << target_chips.size() << " chips, not all chips on the same board";

    // Verify only target chips are present.
    for (ChipId chip : constrained_chips) {
        EXPECT_TRUE(target_chips.count(chip) > 0) << "Chip " << chip << " should not be included (not in target_chips)";
    }

    for (ChipId chip : target_chips) {
        EXPECT_TRUE(constrained_chips.count(chip) > 0) << "Target chip " << chip << " should be included";
    }

    // Test 2: With TT_VISIBLE_DEVICES, should NOT expand to include all chips on the same boards.
    std::string tt_visible_devices_value = "0,15,20,10,2,9,17,29,5,26,12,8,21,7,31,22";
    if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), tt_visible_devices_value.c_str(), 1) != 0) {
        ASSERT_TRUE(false) << "Failed to set TT_VISIBLE_DEVICES environment variable.";
    }

    std::unordered_set<ChipId> expected_chips = {0, 15, 20, 10, 2, 9, 17, 29, 5, 26, 12, 8, 21, 7, 31, 22};
    std::unique_ptr<ClusterDescriptor> constrained_desc_tt =
        ClusterDescriptor::create_constrained_cluster_descriptor(cluster_desc.get(), {});

    ASSERT_NE(constrained_desc_tt, nullptr);
    std::unordered_set<ChipId> constrained_chips_tt = constrained_desc_tt->get_all_chips();

    // Should have exactly the chips specified in TT_VISIBLE_DEVICES (16 chips), not all 32.
    EXPECT_EQ(constrained_chips_tt.size(), expected_chips.size())
        << "Expected exactly " << expected_chips.size() << " chips from TT_VISIBLE_DEVICES, but got "
        << constrained_chips_tt.size();

    // Verify all expected chips from TT_VISIBLE_DEVICES are present.
    for (ChipId expected_chip : expected_chips) {
        EXPECT_TRUE(constrained_chips_tt.count(expected_chip) > 0)
            << "Expected chip " << expected_chip << " from TT_VISIBLE_DEVICES not found in constrained descriptor";
    }

    // Verify no unexpected chips (chips not in TT_VISIBLE_DEVICES).
    for (ChipId chip : constrained_chips_tt) {
        EXPECT_TRUE(expected_chips.count(chip) > 0)
            << "Unexpected chip " << chip << " found in constrained descriptor (not in TT_VISIBLE_DEVICES)";
    }

    // Clean up: unset TT_VISIBLE_DEVICES.
    if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
        ASSERT_TRUE(false) << "Failed to unset TT_VISIBLE_DEVICES environment variable.";
    }
}

TEST(ApiClusterDescriptorOfflineTest, RemoteEthernetConnectionsPreservedWhenConstrained) {
    // Load descriptor that has ethernet_connections_to_remote_devices (N300 with remote links).
    std::string cluster_desc_path = test_utils::GetClusterDescAbsPath("wormhole_N300_with_remote_connections.yaml");
    std::unique_ptr<ClusterDescriptor> full_desc = ClusterDescriptor::create_from_yaml(cluster_desc_path);
    ASSERT_NE(full_desc, nullptr);

    const auto& full_remote = full_desc->get_ethernet_connections_to_remote_devices();
    ASSERT_FALSE(full_remote.empty()) << "Test requires cluster with remote ethernet connections";

    // Constrain to chips 0 and 1 (N300 board expansion keeps both).
    std::unordered_set<ChipId> target_chips = {0, 1};
    std::unique_ptr<ClusterDescriptor> constrained_desc =
        ClusterDescriptor::create_constrained_cluster_descriptor(full_desc.get(), target_chips);

    const auto& constrained_remote = constrained_desc->get_ethernet_connections_to_remote_devices();

    // Remote connections for all visible chips must be preserved.
    for (const auto& [chip_id, remote_conns] : full_remote) {
        if (constrained_desc->get_all_chips().count(chip_id) == 0) {
            continue;
        }
        EXPECT_TRUE(constrained_remote.find(chip_id) != constrained_remote.end())
            << "Chip " << chip_id << "'s remote ethernet connections must be present in constrained descriptor";
        EXPECT_EQ(constrained_remote.at(chip_id).size(), remote_conns.size())
            << "All of chip " << chip_id << "'s remote connections must be preserved";
    }
}

TEST(ApiClusterDescriptorOfflineTest, LocalConnectionsConvertedToRemoteWhenFiltering) {
    // Load T3K cluster descriptor (8 chips: 0-7) and filter to chip 0 only.
    // Chip 0 has connections to chips 1, 2, and 4.
    // After filtering (with board expansion), chip 1 will also be visible (same board as 0).
    // Connections from visible chips to non-visible chips (2-7, except 1) should become remote connections.
    std::unique_ptr<ClusterDescriptor> full_desc =
        ClusterDescriptor::create_from_yaml(test_utils::GetClusterDescAbsPath("t3k_cluster_desc.yaml"));
    ASSERT_NE(full_desc, nullptr);

    std::unordered_set<ChipId> target_chips = {0};
    std::unique_ptr<ClusterDescriptor> constrained_desc =
        ClusterDescriptor::create_constrained_cluster_descriptor(full_desc.get(), target_chips);
    ASSERT_NE(constrained_desc, nullptr);

    // Get actual visible chips after filtering (may include board expansion).
    std::unordered_set<ChipId> visible_chips = constrained_desc->get_all_chips();
    ASSERT_FALSE(visible_chips.empty()) << "Should have at least one visible chip";

    const auto& constrained_local = constrained_desc->get_ethernet_connections();
    const auto& constrained_remote = constrained_desc->get_ethernet_connections_to_remote_devices();
    const auto& chip_unique_ids = full_desc->get_chip_unique_ids();

    // Verify connections from visible chips to non-visible chips are converted to remote connections.
    for (const auto& [chip_id, connections] : full_desc->get_ethernet_connections()) {
        if (visible_chips.count(chip_id) == 0) {
            continue;
        }
        for (const auto& [eth_channel, remote_chip_and_channel] : connections) {
            ChipId remote_chip = std::get<0>(remote_chip_and_channel);
            if (visible_chips.count(remote_chip) == 0) {
                // Should be converted to remote connection with correct unique_id.
                ASSERT_TRUE(constrained_remote.find(chip_id) != constrained_remote.end())
                    << "Chip " << chip_id << " should have remote connections";
                ASSERT_TRUE(constrained_remote.at(chip_id).find(eth_channel) != constrained_remote.at(chip_id).end())
                    << "Connection from chip " << chip_id << " channel " << eth_channel << " to non-visible chip "
                    << remote_chip << " should be in remote connections";
                auto [remote_unique_id, remote_channel] = constrained_remote.at(chip_id).at(eth_channel);
                EXPECT_EQ(remote_unique_id, chip_unique_ids.at(remote_chip))
                    << "Remote unique_id should match chip_unique_ids";
            } else {
                // Should remain as local connection.
                ASSERT_TRUE(constrained_local.find(chip_id) != constrained_local.end())
                    << "Chip " << chip_id << " should have local connections";
                EXPECT_TRUE(constrained_local.at(chip_id).find(eth_channel) != constrained_local.at(chip_id).end())
                    << "Connection between visible chips should remain local";
            }
        }
    }
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
