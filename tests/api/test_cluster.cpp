// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "fmt/xchar.h"
#include "l1_address_map.h"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/mock_chip.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"

// TODO: obviously we need some other way to set this up
#include "noc/noc_parameters.h"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// E75, E150, E300
// N150. N300
// Galaxy

constexpr std::uint32_t L1_BARRIER_BASE = 12;
constexpr std::uint32_t ETH_BARRIER_BASE = 256 * 1024 - 32;
constexpr std::uint32_t DRAM_BARRIER_BASE = 0;

inline std::unique_ptr<Cluster> get_cluster() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        return nullptr;
    }
    return std::unique_ptr<Cluster>(new Cluster());
}

// This test should be one line only.
TEST(ApiClusterTest, OpenAllChips) { std::unique_ptr<Cluster> umd_cluster = get_cluster(); }

TEST(ApiClusterTest, DifferentConstructors) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unique_ptr<Cluster> umd_cluster;

    // 1. Simplest constructor. Creates Cluster with all the chips available.
    umd_cluster = std::make_unique<Cluster>();
    umd_cluster = nullptr;

    // 2. Constructor which allows choosing a subset of Chips to open.
    chip_id_t logical_device_id = 0;
    std::set<chip_id_t> target_devices = {logical_device_id};
    umd_cluster = std::make_unique<Cluster>(target_devices);
    umd_cluster = nullptr;

    // 3. Constructor taking a custom soc descriptor in addition.
    tt::ARCH device_arch = tt_ClusterDescriptor::detect_arch(logical_device_id);
    // You can add a custom soc descriptor here.
    std::string sdesc_path = tt_SocDescriptor::get_soc_descriptor_path(device_arch, BoardType::UNKNOWN);
    umd_cluster = std::make_unique<Cluster>(sdesc_path, target_devices);
    umd_cluster = nullptr;
}

TEST(ApiClusterTest, SimpleIOAllChips) {
    std::unique_ptr<Cluster> umd_cluster = get_cluster();

    if (umd_cluster == nullptr || umd_cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0, "LARGE_WRITE_TLB");

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size, "LARGE_READ_TLB");

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiClusterTest, RemoteFlush) {
    std::unique_ptr<Cluster> umd_cluster = get_cluster();

    if (umd_cluster == nullptr || umd_cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_remote_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        if (!cluster_desc->is_chip_remote(chip_id)) {
            std::cout << "Chip " << chip_id << " skipped because it is not a remote chip." << std::endl;
            continue;
        }

        if (soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;
        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0, "LARGE_WRITE_TLB");

        std::cout << "Waiting for remote chip flush " << chip_id << std::endl;
        umd_cluster->wait_for_non_mmio_flush(chip_id);

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;
        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size, "LARGE_READ_TLB");

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiClusterTest, SimpleIOSpecificChips) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>(0u);

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0, "LARGE_WRITE_TLB");

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size, "LARGE_READ_TLB");

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ClusterAPI, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for
    // each transaction

    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unique_ptr<Cluster> cluster = get_cluster();

    tt_device_params default_params;
    cluster->start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = zeros;

    static const uint32_t num_loops = 100;

    std::set<chip_id_t> target_devices = cluster->get_target_device_ids();
    for (const chip_id_t chip : target_devices) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        // Write to each core a 100 times at different statically mapped addresses
        const tt_SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip);
        std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);
        for (int loop = 0; loop < num_loops; loop++) {
            for (auto& core : tensix_cores) {
                cluster->write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    chip,
                    core,
                    address,
                    "SMALL_READ_WRITE_TLB");

                // Barrier to ensure that all writes over ethernet were commited
                cluster->wait_for_non_mmio_flush();
                cluster->read_from_device(readback_vec.data(), chip, core, address, 40, "SMALL_READ_WRITE_TLB");

                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";

                cluster->wait_for_non_mmio_flush();

                cluster->write_to_device(
                    zeros.data(), zeros.size() * sizeof(std::uint32_t), chip, core, address, "SMALL_READ_WRITE_TLB");

                cluster->wait_for_non_mmio_flush();

                readback_vec = zeros;
            }
            address += 0x20;  // Increment by uint32_t size for each write
        }
    }
    cluster->close_device();
}

TEST(ClusterAPI, TestClusterSerialize) {
    std::string cluster_path = tt::umd::Cluster::serialize();
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(
        tt_ClusterDescriptor::create_from_yaml(cluster_path), 1, false, false, true, simulated_harvesting_masks, true);
}
