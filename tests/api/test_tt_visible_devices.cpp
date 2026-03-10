// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "utils.hpp"

using namespace tt::umd;

TEST(TestTTVisibleDevices, OpenChipsById) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    // T3K and 4U have 4 PCIE visible devices each. After 4 devices, the next number is 32
    // on 6U galaxies. Making 2^32 combinations might take too long, so we limit the number of devices to 4.
    // TODO: test all combinations on 6U and remove this check if possible.
    if (pci_device_ids.size() > 4) {
        GTEST_SKIP() << "Skipping test because there are more than 4 PCI devices. "
                        "This test is intended to be run on all systems apart from 6U.";
    }

    int total_combinations = 1 << pci_device_ids.size();

    for (uint32_t combination = 0; combination < total_combinations; combination++) {
        std::unordered_set<int> target_device_ids;
        for (int i = 0; i < pci_device_ids.size(); i++) {
            if (combination & (1 << i)) {
                target_device_ids.insert(i);
            }
        }

        std::cout << "Creating Cluster with target device IDs: ";
        for (const auto& id : target_device_ids) {
            std::cout << id << " ";
        }
        std::cout << std::endl;

        std::string value = test_utils::convert_to_comma_separated_string(target_device_ids);

        if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), value.c_str(), 1) != 0) {
            ASSERT_TRUE(false) << "Failed to set environment variable.";
        }

        // Make sure that Cluster construction is without exceptions.
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

        if (!target_device_ids.empty()) {
            // If target_device_ids is empty, then full cluster will be created, so skip the check.
            // Check that the cluster has the expected number of chips.
            auto actual_pci_device_ids = cluster->get_target_mmio_device_ids();
            EXPECT_EQ(actual_pci_device_ids.size(), target_device_ids.size());
            // Always expect logical id 0 to exist, that's the way filtering by ids work.
            EXPECT_TRUE(actual_pci_device_ids.find(0) != actual_pci_device_ids.end());
        }

        if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
            ASSERT_TRUE(false) << "Failed to unset environment variable.";
        }
    }
}

TEST(TestTTVisibleDevices, OpenChipsByBDF) {
    // Get all available PCI devices and their BDF addresses.
    auto device_info_map = PCIDevice::enumerate_devices_info();

    // Extract BDF addresses.
    std::vector<std::string> pci_bdf_addresses;
    pci_bdf_addresses.reserve(device_info_map.size());
    for (const auto& [device_id, info] : device_info_map) {
        pci_bdf_addresses.push_back(info.pci_bdf);
    }

    // Limit combinations like the original test.
    if (pci_bdf_addresses.size() > 4) {
        GTEST_SKIP() << "Skipping test because there are more than 4 PCI devices. "
                        "This test is intended to be run on all systems apart from 6U.";
    }

    int total_combinations = 1 << pci_bdf_addresses.size();

    for (uint32_t combination = 0; combination < total_combinations; combination++) {
        std::vector<std::string> target_bdf_addresses;
        target_bdf_addresses.reserve(pci_bdf_addresses.size());
        for (int i = 0; i < pci_bdf_addresses.size(); i++) {
            if (combination & (1 << i)) {
                target_bdf_addresses.push_back(pci_bdf_addresses[i]);
            }
        }

        std::cout << "Creating Cluster with target BDF addresses: ";
        for (const auto& bdf : target_bdf_addresses) {
            std::cout << bdf << " ";
        }
        std::cout << std::endl;

        // Convert BDF addresses to comma-separated string.
        std::string bdf_value;
        for (size_t i = 0; i < target_bdf_addresses.size(); ++i) {
            if (i > 0) {
                bdf_value += ",";
            }
            bdf_value += target_bdf_addresses[i];
        }

        if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), bdf_value.c_str(), 1) != 0) {
            ASSERT_TRUE(false) << "Failed to set TT_VISIBLE_DEVICES environment variable.";
        }

        // Make sure that Cluster construction is without exceptions.
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

        // Check that the cluster has the expected number of chips.
        auto actual_pci_device_ids = cluster->get_target_mmio_device_ids();
        EXPECT_EQ(actual_pci_device_ids.size(), target_bdf_addresses.size());

        if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
            ASSERT_TRUE(false) << "Failed to unset TT_VISIBLE_DEVICES environment variable.";
        }
    }
}

TEST(TestTTVisibleDevices, OpenChipsByBDFWormhole6U) {
    // Get all available PCI devices and their BDF addresses.
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_tt_device(0)->get_board_type() != BoardType::UBB_WORMHOLE) {
        GTEST_SKIP() << "This test is intended to be run on Wormhole 6U systems only.";
    }

    std::string bdf_value = "0000:01:00.0, 0000:02:00.0, 0000:03:00.0, 0000:04:00.0";

    if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), bdf_value.c_str(), 1) != 0) {
        ASSERT_TRUE(false) << "Failed to set TT_VISIBLE_DEVICES environment variable.";
    }

    // Make sure that Cluster construction is without exceptions.
    std::unique_ptr<Cluster> cluster_tt_visible_devices = std::make_unique<Cluster>();

    // Check that the cluster has the expected number of chips.
    auto actual_pci_device_ids = cluster_tt_visible_devices->get_target_mmio_device_ids();
    EXPECT_EQ(actual_pci_device_ids.size(), 4);

    if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
        ASSERT_TRUE(false) << "Failed to unset TT_VISIBLE_DEVICES environment variable.";
    }
}

TEST(TestTTVisibleDevices, OpenChipsByBDFWormhole6USameChip) {
    // Get all available PCI devices and their BDF addresses.
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_tt_device(0)->get_board_type() != BoardType::UBB_WORMHOLE) {
        GTEST_SKIP() << "This test is intended to be run on Wormhole 6U systems only.";
    }

    // This BDF and logical ID 0 should represent the same device on galaxy 6U system.
    std::string filter_value = "0000:01:00.0,0";

    if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), filter_value.c_str(), 1) != 0) {
        ASSERT_TRUE(false) << "Failed to set TT_VISIBLE_DEVICES environment variable.";
    }

    // Make sure that Cluster construction is without exceptions.
    std::unique_ptr<Cluster> cluster_tt_visible_devices = std::make_unique<Cluster>();

    // Check that the cluster has the expected number of chips, which is 1 because of the BDF being the lowest out of
    // all chips on galaxy, which is at the same time represented by logical ID 0.
    auto actual_pci_device_ids = cluster_tt_visible_devices->get_target_mmio_device_ids();
    EXPECT_EQ(actual_pci_device_ids.size(), 1);

    if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
        ASSERT_TRUE(false) << "Failed to unset TT_VISIBLE_DEVICES environment variable.";
    }
}

TEST(TestTTVisibleDevices, OpenChipsByBDFWormhole6UPattern) {
    // Get all available PCI devices and their BDF addresses.
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_tt_device(0)->get_board_type() != BoardType::UBB_WORMHOLE) {
        GTEST_SKIP() << "This test is intended to be run on Wormhole 6U systems only.";
    }

    std::string bdf_value = "0000:c";

    if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), bdf_value.c_str(), 1) != 0) {
        ASSERT_TRUE(false) << "Failed to set TT_VISIBLE_DEVICES environment variable.";
    }

    // Make sure that Cluster construction is without exceptions.
    std::unique_ptr<Cluster> cluster_tt_visible_devices = std::make_unique<Cluster>();

    // Check that the cluster has the expected number of chips. By pattern in TT_VISIBLE_DEVICES, we should select full
    // tray of chips, which is 8 chips in total.
    auto actual_pci_device_ids = cluster_tt_visible_devices->get_target_mmio_device_ids();
    EXPECT_EQ(actual_pci_device_ids.size(), 8);

    if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
        ASSERT_TRUE(false) << "Failed to unset TT_VISIBLE_DEVICES environment variable.";
    }
}

TEST(TestTTVisibleDevices, OpenChipsByIdException) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    std::unordered_set<int> target_device_ids;
    target_device_ids.insert(pci_device_ids.size());

    std::cout << "Creating Cluster with target device IDs: ";
    for (const auto& id : target_device_ids) {
        std::cout << id << " ";
    }
    std::cout << std::endl;

    std::string value = test_utils::convert_to_comma_separated_string(target_device_ids);

    if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), value.c_str(), 1) != 0) {
        ASSERT_TRUE(false) << "Failed to set environment variable.";
    }

    // Since target ID is not in the range of available devices, expect an exception.
    EXPECT_THROW(std::make_unique<Cluster>(), std::runtime_error);

    if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
        ASSERT_TRUE(false) << "Failed to unset environment variable.";
    }
}

TEST(TestTTVisibleDevices, LogicalIdMatchesEnumerateDevicesOrder) {
    std::vector<int> enumerated_ids = PCIDevice::enumerate_devices();

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    // Verify that for each logical ID i, the PCI device behind it matches
    // the i-th device returned by enumerate_devices() (which is BDF-sorted).
    for (const ChipId chip_id : cluster->get_target_mmio_device_ids()) {
        TTDevice* tt_device = cluster->get_tt_device(chip_id);
        ASSERT_NE(tt_device, nullptr) << "No TTDevice found for logical ID " << chip_id;
        std::shared_ptr<PCIDevice> pci_device = tt_device->get_pci_device();
        ASSERT_NE(pci_device, nullptr) << "No PCI device found for logical ID " << chip_id;
        EXPECT_EQ(pci_device->get_device_num(), enumerated_ids[chip_id])
            << "Chip ID " << chip_id << " maps to PCI device " << pci_device->get_device_num()
            << " but enumerate_devices() returned " << enumerated_ids[chip_id] << " at index " << chip_id;
    }
}

TEST(TestTTVisibleDevices, DifferentConstructors) {
    std::unique_ptr<Cluster> umd_cluster;

    // 1. Simplest constructor. Creates Cluster with all the chips available.
    umd_cluster = std::make_unique<Cluster>();
    bool chips_available = !umd_cluster->get_target_device_ids().empty();
    umd_cluster.reset();

    if (chips_available) {
        // 2. Constructor taking a custom soc descriptor in addition.
        tt::ARCH device_arch = Cluster::create_cluster_descriptor()->get_arch(0);
        // You can add a custom soc descriptor here.
        std::string sdesc_path = test_utils::get_soc_descriptor_path(device_arch);
        umd_cluster = std::make_unique<Cluster>(ClusterOptions{
            .sdesc_path = sdesc_path,
        });
        umd_cluster.reset();
    }

    // 3. Constructor taking cluster descriptor based on which to create cluster.
    // This could be cluster descriptor cached from previous runtime, or with some custom modifications.
    // You can just create a cluster descriptor and serialize it to file, or fetch a cluster descriptor from already
    // created Cluster class.
    std::filesystem::path cluster_path1 = Cluster::create_cluster_descriptor()->serialize_to_file();
    umd_cluster = std::make_unique<Cluster>();
    umd_cluster->get_cluster_description()->serialize_to_file();
    umd_cluster.reset();

    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(cluster_path1);
    umd_cluster = std::make_unique<Cluster>(ClusterOptions{
        .cluster_descriptor = cluster_desc.get(),
    });
    umd_cluster.reset();

    // 4. Create mock chips is set to true in order to create mock chips for the devices in the cluster descriptor.
    umd_cluster = std::make_unique<Cluster>(ClusterOptions{
        .chip_type = ChipType::MOCK,
        .target_devices = {0},
    });
    umd_cluster.reset();
}
