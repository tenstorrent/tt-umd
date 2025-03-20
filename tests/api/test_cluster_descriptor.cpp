// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <fstream>

#include "disjoint_set.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/pci_device.hpp"
#include "umd/device/tt_cluster_descriptor.h"

using namespace tt::umd;

TEST(ApiClusterDescriptorTest, DetectArch) {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt::umd::Cluster::create_cluster_descriptor();

    if (cluster_desc->get_number_of_chips() == 0) {
        // Expect it to be invalid if no devices are found.
        EXPECT_THROW(cluster_desc->get_arch(0), std::runtime_error);
    } else {
        tt::ARCH arch = cluster_desc->get_arch(0);
        EXPECT_NE(arch, tt::ARCH::Invalid);

        // Test that cluster descriptor and PCIDevice::enumerate_devices_info() return the same set of chips.
        std::map<int, PciDeviceInfo> pci_device_infos = PCIDevice::enumerate_devices_info();
        std::unordered_set<chip_id_t> pci_chips_set;
        for (auto [pci_device_number, _] : pci_device_infos) {
            pci_chips_set.insert(pci_device_number);
        }

        std::unordered_map<chip_id_t, chip_id_t> chips_with_mmio = cluster_desc->get_chips_with_mmio();
        std::unordered_set<chip_id_t> cluster_chips_set;
        for (auto [_, pci_device_number] : chips_with_mmio) {
            cluster_chips_set.insert(pci_device_number);
        }

        EXPECT_EQ(pci_chips_set, cluster_chips_set);

        // Test that cluster descriptor holds the same arch as pci_device.
        for (auto [chip, pci_device_number] : cluster_desc->get_chips_with_mmio()) {
            EXPECT_EQ(cluster_desc->get_arch(chip), pci_device_infos.at(pci_device_number).get_arch());
        }
    }
}

TEST(ApiClusterDescriptorTest, BasicFunctionality) {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt::umd::Cluster::create_cluster_descriptor();

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
             "blackhole_P100.yaml",
             "galaxy.yaml",
             "wormhole_2xN300_unconnected.yaml",
             "wormhole_N150.yaml",
             "wormhole_N300.yaml",
             "wormhole_N300_routing_info.yaml",
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

        // Check that cluster_id is always the same for the same cluster.
        // Cluster id takes the value of the smallest chip_id in the cluster.
        for (auto const &[chip, coord] : eth_chip_coords) {
            if (cluster_desc_yaml != "wormhole_2xN300_unconnected.yaml") {
                EXPECT_EQ(coord.cluster_id, 0);
            } else {
                EXPECT_TRUE(coord.cluster_id == 0 || coord.cluster_id == 1);
            }
        }
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
    for (auto chip : all_chips) {
        chip_id_t closest_mmio_chip = cluster_desc->get_closest_mmio_capable_chip(chip);
        EXPECT_TRUE(chip_clusters.are_same_set(chip, closest_mmio_chip));
    }
}

TEST(ApiClusterDescriptorTest, EthernetConnectivity) {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt::umd::Cluster::create_cluster_descriptor();

    if (cluster_desc == nullptr) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    auto ethernet_connections = cluster_desc->get_ethernet_connections();
    for (auto [chip, connections] : ethernet_connections) {
        for (auto [channel, remote_chip_and_channel] : connections) {
            std::cout << "Ethernet connection from chip " << chip << " channel " << channel << " to chip "
                      << std::get<0>(remote_chip_and_channel) << " channel " << std::get<1>(remote_chip_and_channel)
                      << std::endl;
        }
    }

    auto chips_with_mmio = cluster_desc->get_chips_with_mmio();
    for (auto [chip, mmio_chip] : chips_with_mmio) {
        std::cout << "Chip " << chip << " has MMIO on PCI id " << mmio_chip << std::endl;
    }

    for (auto chip : cluster_desc->get_all_chips()) {
        // Wormhole has 16 and Blackhole has 14 ethernet channels.
        for (int eth_chan = 0;
             eth_chan <
             tt::umd::architecture_implementation::create(cluster_desc->get_arch(chip))->get_num_eth_channels();
             eth_chan++) {
            bool has_active_link = cluster_desc->ethernet_core_has_active_ethernet_link(chip, eth_chan);
            std::cout << "Chip " << chip << " channel " << eth_chan << " has active link: " << has_active_link
                      << std::endl;

            if (!has_active_link) {
                continue;
            }
            std::tuple<chip_id_t, ethernet_channel_t> remote_chip_and_channel =
                cluster_desc->get_chip_and_channel_of_remote_ethernet_core(chip, eth_chan);
            std::cout << "Chip " << chip << " channel " << eth_chan << " has remote chip "
                      << std::get<0>(remote_chip_and_channel) << " channel " << std::get<1>(remote_chip_and_channel)
                      << std::endl;
        }
    }

    for (auto chip : cluster_desc->get_all_chips()) {
        std::cout << "Chip " << chip << " has the following active ethernet channels: ";
        for (auto eth_chan : cluster_desc->get_active_eth_channels(chip)) {
            std::cout << eth_chan << " ";
        }
        std::cout << std::endl;
        std::cout << " and following idle ethernet channels: ";
        for (auto eth_chan : cluster_desc->get_idle_eth_channels(chip)) {
            std::cout << eth_chan << " ";
        }
        std::cout << std::endl;
    }
}

TEST(ApiClusterDescriptorTest, PrintClusterDescriptor) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.size() == 0) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));

    // In case of u6 galaxy and blackhole, we generate the cluster descriptor.
    // For wormhole we still use create-ethernet-map.
    std::filesystem::path cluster_path;
    if (tt_device->get_arch() == tt::ARCH::BLACKHOLE || tt_device->get_board_type() == BoardType::UBB) {
        cluster_path = tt::umd::Cluster::serialize_to_file();
    } else {
        cluster_path = tt_ClusterDescriptor::get_cluster_descriptor_file_path();
    }

    std::cout << "Cluster descriptor file path: " << cluster_path << std::endl;
    std::cout << "Contents:" << std::endl;
    std::ifstream file(cluster_path);  // open the file
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << std::endl;
    }
    file.close();
}
