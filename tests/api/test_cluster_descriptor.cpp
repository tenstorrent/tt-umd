// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "common/disjoint_set.hpp"
#include "device/pcie/pci_device.hpp"
#include "device/tt_cluster_descriptor.h"
#include "tests/test_utils/generate_cluster_desc.hpp"

// TODO: Needed for detect_arch, remove when it is part of cluster descriptor.
#include "device/cluster.h"

inline std::unique_ptr<tt_ClusterDescriptor> get_cluster_desc() {
    // TODO: remove getting manually cluster descriptor from yaml.
    std::string yaml_path = tt_ClusterDescriptor::get_cluster_descriptor_file_path();

    return tt_ClusterDescriptor::create_from_yaml(yaml_path);
}

TEST(ApiClusterDescriptorTest, DetectArch) {
    // TODO: This should be part of cluster descriptor. It is currently used like this from tt_metal.
    tt::ARCH arch = detect_arch();

    // Expect it to be invalid if no devices are found.
    if (PCIDevice::enumerate_devices().empty()) {
        EXPECT_EQ(arch, tt::ARCH::Invalid);
    } else {
        EXPECT_NE(arch, tt::ARCH::Invalid);

        // TODO: This should be the only available API, previous call should be routed to this one to get any arch.
        tt::ARCH arch2 = detect_arch(PCIDevice::enumerate_devices()[0]);
        EXPECT_NE(arch2, tt::ARCH::Invalid);

        // In our current setup, we expect all arch to be the same.
        EXPECT_EQ(arch, arch2);
    }
}

TEST(ApiClusterDescriptorTest, BasicFunctionality) {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = get_cluster_desc();

    if (cluster_desc == nullptr) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unordered_set<chip_id_t> all_chips = cluster_desc->get_all_chips();
    std::unordered_map<chip_id_t, std::uint32_t> harvesting_for_chips = cluster_desc->get_harvesting_info();
    std::unordered_map<chip_id_t, eth_coord_t> eth_chip_coords = cluster_desc->get_chip_locations();
    std::unordered_map<chip_id_t, chip_id_t> local_chips_to_pci_device_id = cluster_desc->get_chips_with_mmio();
    std::unordered_set<chip_id_t> local_chips;
    for (auto [chip, _] : local_chips_to_pci_device_id) {
        local_chips.insert(chip);
    }
    std::unordered_set<chip_id_t> remote_chips;
    for (auto chip : all_chips) {
        if (local_chips.find(chip) == local_chips.end()) {
            remote_chips.insert(chip);
        }
    }

    std::unordered_map<chip_id_t, std::unordered_set<chip_id_t>> chips_grouped_by_closest_mmio =
        cluster_desc->get_chips_grouped_by_closest_mmio();
}

TEST(ApiClusterDescriptorTest, TestAllOfflineClusterDescriptors) {
    for (std::string cluster_desc_yaml : {
             "blackhole_P150.yaml",
             "galaxy.yaml",
             "grayskull_E150.yaml",
             "grayskull_E300.yaml",
             "wormhole_2xN300_unconnected.yaml",
             "wormhole_N150.yaml",
             "wormhole_N300.yaml",
         }) {
        std::cout << "Testing " << cluster_desc_yaml << std::endl;
        std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(
            test_utils::GetAbsPath("tests/api/cluster_descriptor_examples/" + cluster_desc_yaml));

        std::unordered_set<chip_id_t> all_chips = cluster_desc->get_all_chips();
        std::unordered_map<chip_id_t, std::uint32_t> harvesting_for_chips = cluster_desc->get_harvesting_info();
        std::unordered_map<chip_id_t, eth_coord_t> eth_chip_coords = cluster_desc->get_chip_locations();
        std::unordered_map<chip_id_t, chip_id_t> local_chips_to_pci_device_id = cluster_desc->get_chips_with_mmio();
        std::unordered_set<chip_id_t> local_chips;
        for (auto [chip, _] : local_chips_to_pci_device_id) {
            local_chips.insert(chip);
        }
        std::unordered_set<chip_id_t> remote_chips;
        for (auto chip : all_chips) {
            if (local_chips.find(chip) == local_chips.end()) {
                remote_chips.insert(chip);
            }
        }

        std::unordered_map<chip_id_t, std::unordered_set<chip_id_t>> chips_grouped_by_closest_mmio =
            cluster_desc->get_chips_grouped_by_closest_mmio();
    }
}

TEST(ApiClusterDescriptorTest, SeparateClusters) {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(
        test_utils::GetAbsPath("tests/api/cluster_descriptor_examples/wormhole_2xN300_unconnected.yaml"));

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
    // Currently, it is expected that the following fails if there is more than 1 cluster.
    for (auto chip : all_chips) {
        chip_id_t closest_mmio_chip = cluster_desc->get_closest_mmio_capable_chip(chip);
        EXPECT_TRUE(chip_clusters.are_same_set(chip, closest_mmio_chip));
    }
}
