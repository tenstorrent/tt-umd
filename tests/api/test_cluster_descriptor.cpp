// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <fstream>

#include "disjoint_set.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/pcie/pci_device.hpp"

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

TEST(ApiClusterDescriptorTest, DetectArch) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();

    if (cluster_desc->get_number_of_chips() == 0) {
        // Expect it to be invalid if no devices are found.
        EXPECT_THROW(cluster_desc->get_arch(0), std::runtime_error);
    } else {
        tt::ARCH arch = cluster_desc->get_arch(0);
        EXPECT_NE(arch, tt::ARCH::Invalid);

        // Test that cluster descriptor and PCIDevice::enumerate_devices_info() return the same set of chips.
        std::map<int, PciDeviceInfo> pci_device_infos = PCIDevice::enumerate_devices_info();
        std::unordered_set<ChipId> pci_chips_set;
        for (auto [pci_device_number, _] : pci_device_infos) {
            pci_chips_set.insert(pci_device_number);
        }

        std::unordered_map<ChipId, ChipId> chips_with_mmio = cluster_desc->get_chips_with_mmio();
        std::unordered_set<ChipId> cluster_chips_set;
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
    std::unique_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();

    if (cluster_desc == nullptr) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unordered_set<ChipId> all_chips = cluster_desc->get_all_chips();
    std::unordered_map<ChipId, EthCoord> eth_chip_coords = cluster_desc->get_chip_locations();
    std::unordered_map<ChipId, ChipId> local_chips_to_pci_device_id = cluster_desc->get_chips_with_mmio();
    std::unordered_set<ChipId> local_chips;
    std::unordered_set<ChipId> remote_chips;

    for (auto chip_id : all_chips) {
        if (cluster_desc->is_chip_mmio_capable(chip_id)) {
            local_chips.insert(chip_id);
        }
        if (cluster_desc->is_chip_remote(chip_id)) {
            remote_chips.insert(chip_id);
        }

        auto harvesting_masks = cluster_desc->get_harvesting_masks(chip_id);
    }

    std::unordered_map<ChipId, std::unordered_set<ChipId>> chips_grouped_by_closest_mmio =
        cluster_desc->get_chips_grouped_by_closest_mmio();
}

TEST(ApiClusterDescriptorTest, EthernetConnectivity) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();

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
             eth_chan < architecture_implementation::create(cluster_desc->get_arch(chip))->get_num_eth_channels();
             eth_chan++) {
            bool has_active_link = cluster_desc->ethernet_core_has_active_ethernet_link(chip, eth_chan);
            std::cout << "Chip " << chip << " channel " << eth_chan << " has active link: " << has_active_link
                      << std::endl;

            if (!has_active_link) {
                continue;
            }
            std::tuple<ChipId, EthernetChannel> remote_chip_and_channel =
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

    // In case of u6 galaxy and blackhole, we generate the cluster descriptor.
    // For wormhole we still use create-ethernet-map.
    std::filesystem::path cluster_path;
    cluster_path = Cluster::create_cluster_descriptor()->serialize_to_file();

    std::cout << "Cluster descriptor file path: " << cluster_path << std::endl;
    std::cout << "Contents:" << std::endl;
    std::ifstream file(cluster_path);  // open the file
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << std::endl;
    }
    file.close();
}

TEST(ApiClusterDescriptorTest, VerifyEthConnections) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();

    std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<ChipId, EthernetChannel>>>
        eth_connections = cluster_desc->get_ethernet_connections();
    // Check that all ethernet connections are bidirectional.
    for (const auto& [chip, connections] : cluster_desc->get_ethernet_connections()) {
        for (const auto& [channel, remote_chip_and_channel] : connections) {
            auto [remote_chip, remote_channel] = remote_chip_and_channel;

            ASSERT_TRUE(eth_connections.find(remote_chip) != eth_connections.end())
                << "Remote chip " << remote_chip << " not found in ethernet connections.";
            ASSERT_TRUE(eth_connections.at(remote_chip).find(remote_channel) != eth_connections.at(remote_chip).end())
                << "Remote channel " << remote_channel << " not found in ethernet connections for remote chip "
                << remote_chip;
        }
    }
}

/**
 * This test is used to verify that we are running on some well known topologies.
 * Since UMD can be run in custom topologies, this is mostly used for CI, to try and verify
 * that we don't have problems on standard topologies. However, bugs could lead to T3K being recognizible as
 * single N300 or something similar, but this should raise our confidence of standard topologies working as
 * expected.
 */
TEST(ApiClusterDescriptorTest, VerifyStandardTopology) {
    std::unique_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();

    auto all_chips = cluster_desc->get_all_chips();

    if (all_chips.size() == 0) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    switch (all_chips.size()) {
        // This covers N150, P100, P150.
        case 1: {
            auto chips_with_mmio = cluster_desc->get_chips_with_mmio();
            EXPECT_EQ(chips_with_mmio.size(), 1);

            auto eth_connections = cluster_desc->get_ethernet_connections();
            EXPECT_EQ(count_connections(eth_connections), 0);

            for (auto chip : all_chips) {
                BoardType board_type = cluster_desc->get_board_type(chip);
                EXPECT_TRUE(
                    board_type == BoardType::N150 || board_type == BoardType::P100 || board_type == BoardType::P150)
                    << "Unexpected board type for chip " << chip << ": " << static_cast<int>(board_type);
            }
            break;
        }

        // This covers N300, P300.
        case 2: {
            auto chips_with_mmio = cluster_desc->get_chips_with_mmio();

            const BoardType board_type = cluster_desc->get_board_type(*all_chips.begin());
            if (board_type == BoardType::N300) {
                EXPECT_EQ(chips_with_mmio.size(), 1);
            } else if (board_type == BoardType::P300) {
                EXPECT_TRUE(chips_with_mmio.size() == 1 || chips_with_mmio.size() == 2)
                    << "Unexpected number of mmio capable chips for P300: " << chips_with_mmio.size();
            }

            auto eth_connections = cluster_desc->get_ethernet_connections();
            EXPECT_EQ(count_connections(eth_connections), 4);

            for (auto chip : all_chips) {
                BoardType board_type = cluster_desc->get_board_type(chip);
                EXPECT_TRUE(board_type == BoardType::N300 || board_type == BoardType::P300)
                    << "Unexpected board type for chip " << chip << ": " << static_cast<int>(board_type);
            }
            break;
        }

        // This covers T3K.
        case 8: {
            auto chips_with_mmio = cluster_desc->get_chips_with_mmio();
            EXPECT_EQ(chips_with_mmio.size(), 4);

            auto eth_connections = cluster_desc->get_ethernet_connections();
            EXPECT_EQ(count_connections(eth_connections), 40);

            for (auto chip : all_chips) {
                BoardType board_type = cluster_desc->get_board_type(chip);
                EXPECT_TRUE(board_type == BoardType::N300)
                    << "Unexpected board type for chip " << chip << ": " << static_cast<int>(board_type);
            }
            break;
        }

        // This covers 6U galaxy.
        case 32: {
            auto chips_with_mmio = cluster_desc->get_chips_with_mmio();
            EXPECT_EQ(chips_with_mmio.size(), 32);

            auto eth_connections = cluster_desc->get_ethernet_connections();
            EXPECT_EQ(count_connections(eth_connections), 512);

            for (auto chip : all_chips) {
                BoardType board_type = cluster_desc->get_board_type(chip);
                EXPECT_TRUE(board_type == BoardType::UBB)
                    << "Unexpected board type for chip " << chip << ": " << static_cast<int>(board_type);
            }
            break;
        }

        // This covers 4U galaxy.
        case 36: {
            auto chips_with_mmio = cluster_desc->get_chips_with_mmio();
            EXPECT_EQ(chips_with_mmio.size(), 4);

            auto eth_connections = cluster_desc->get_ethernet_connections();
            EXPECT_EQ(count_connections(eth_connections), 432);

            size_t count_n150 = 0;
            for (auto chip : all_chips) {
                BoardType board_type = cluster_desc->get_board_type(chip);
                EXPECT_TRUE(board_type == BoardType::N150 || board_type == BoardType::GALAXY)
                    << "Unexpected board type for chip " << chip << ": " << static_cast<int>(board_type);
                if (board_type == BoardType::N150) {
                    count_n150++;
                }
            }
            EXPECT_EQ(count_n150, 4) << "Expected 4 N150 chips in 4U galaxy, found " << count_n150;
            break;
        }

        default: {
            throw std::runtime_error(
                "Unexpected number of chips in the cluster descriptor: " + std::to_string(all_chips.size()));
        }
    }
}
