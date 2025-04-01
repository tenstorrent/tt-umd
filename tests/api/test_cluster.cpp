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
#include "umd/device/blackhole_implementation.h"
#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/mock_chip.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/wormhole_implementation.h"

// TODO: obviously we need some other way to set this up
#include "noc/noc_parameters.h"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
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
    tt::ARCH device_arch = Cluster::create_cluster_descriptor()->get_arch(logical_device_id);
    // You can add a custom soc descriptor here.
    std::string sdesc_path = tt_SocDescriptor::get_soc_descriptor_path(device_arch);
    umd_cluster = std::make_unique<Cluster>(sdesc_path, target_devices);
    umd_cluster = nullptr;

    // 4. Constructor taking cluster descriptor based on which to create cluster.
    // Create mock chips is set to true in order to create mock chips for the devices in the cluster descriptor.
    std::filesystem::path cluster_path = tt::umd::Cluster::serialize_to_file();
    std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(
        tt_ClusterDescriptor::create_from_yaml(cluster_path), 1, true, false, true, simulated_harvesting_masks);
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

    std::set<chip_id_t> target_devices = {0};
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>(target_devices);

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

TEST(TestCluster, PrintAllChipsAllCores) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unique_ptr<Cluster> umd_cluster = get_cluster();

    for (chip_id_t chip : umd_cluster->get_target_device_ids()) {
        std::cout << "Chip " << chip << std::endl;

        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip);

        const std::vector<CoreCoord>& tensix_cores = soc_desc.get_cores(CoreType::TENSIX);
        for (const CoreCoord& core : tensix_cores) {
            std::cout << "Tensix core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& dram_cores = soc_desc.get_cores(CoreType::DRAM);
        for (const CoreCoord& core : dram_cores) {
            std::cout << "DRAM core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& eth_cores = soc_desc.get_cores(CoreType::ETH);
        for (const CoreCoord& core : eth_cores) {
            std::cout << "ETH core " << core.str() << std::endl;
        }
    }
}

// It is expected that logical ETH channel numbers are in the range [0, num_channels) for each
// chip. This is needed because of eth id readouts for Blackhole that don't take harvesting into
// acount. This test verifies that both for Wormhole and Blackhole.
TEST(TestCluster, TestClusterLogicalETHChannelsConnectivity) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    tt_ClusterDescriptor* cluster_desc = cluster->get_cluster_description();

    for (auto [chip, connections] : cluster_desc->get_ethernet_connections()) {
        const uint32_t num_channels_local_chip = cluster->get_soc_descriptor(chip).get_cores(CoreType::ETH).size();
        for (auto [channel, remote_chip_and_channel] : connections) {
            auto [remote_chip, remote_channel] = remote_chip_and_channel;

            const uint32_t num_channels_remote_chip =
                cluster->get_soc_descriptor(remote_chip).get_cores(CoreType::ETH).size();

            EXPECT_TRUE(channel < num_channels_local_chip);
            EXPECT_TRUE(remote_channel < num_channels_remote_chip);
        }
    }
}

TEST(TestCluster, TestClusterNocId) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    tt::ARCH arch = cluster->get_cluster_description()->get_arch(0);

    // All chips in the cluster have the same noc_translation_enabled value.
    bool noc_translation_enabled = cluster->get_cluster_description()->get_noc_translation_table_en().at(0);

    uint64_t noc_node_id_reg_addr = 0;
    if (arch == tt::ARCH::WORMHOLE_B0) {
        if (noc_translation_enabled) {
            noc_node_id_reg_addr = tt::umd::wormhole::NOC_CONTROL_REG_ADDR_BASE + tt::umd::wormhole::NOC_CFG_OFFSET +
                                   tt::umd::wormhole::NOC_REG_WORD_SIZE * tt::umd::wormhole::NOC_CFG_NOC_ID_LOGICAL;
        } else {
            noc_node_id_reg_addr = tt::umd::wormhole::NOC_CONTROL_REG_ADDR_BASE + tt::umd::wormhole::NOC_NODE_ID_OFFSET;
        }
    } else if (arch == tt::ARCH::BLACKHOLE) {
        noc_node_id_reg_addr = tt::umd::blackhole::NOC_CONTROL_REG_ADDR_BASE + tt::umd::blackhole::NOC_NODE_ID_OFFSET;
    }

    auto read_noc_id_reg = [noc_node_id_reg_addr](std::unique_ptr<Cluster>& cluster, chip_id_t chip, CoreCoord core) {
        uint32_t noc_node_id_val;
        cluster->read_from_device(
            &noc_node_id_val, chip, core, noc_node_id_reg_addr, sizeof(noc_node_id_val), "REG_TLB");
        uint32_t x = noc_node_id_val & 0x3F;
        uint32_t y = (noc_node_id_val >> 6) & 0x3F;
        return tt_xy_pair(x, y);
    };

    auto check_noc_id_cores = [read_noc_id_reg](std::unique_ptr<Cluster>& cluster, chip_id_t chip, CoreType core_type) {
        const std::vector<CoreCoord>& cores = cluster->get_soc_descriptor(chip).get_cores(core_type);
        for (const CoreCoord& core : cores) {
            const auto [x, y] = read_noc_id_reg(cluster, chip, core);
            CoreCoord translated_coord =
                cluster->get_soc_descriptor(chip).translate_coord_to(core, CoordSystem::TRANSLATED);
            EXPECT_EQ(translated_coord.x, x);
            EXPECT_EQ(translated_coord.y, y);
        }
    };

    auto check_noc_id_harvested_cores = [read_noc_id_reg](
                                            std::unique_ptr<Cluster>& cluster, chip_id_t chip, CoreType core_type) {
        const std::vector<CoreCoord>& cores = cluster->get_soc_descriptor(chip).get_harvested_cores(core_type);
        for (const CoreCoord& core : cores) {
            const auto [x, y] = read_noc_id_reg(cluster, chip, core);
            CoreCoord translated_coord =
                cluster->get_soc_descriptor(chip).translate_coord_to(core, CoordSystem::TRANSLATED);
            EXPECT_EQ(translated_coord.x, x);
            EXPECT_EQ(translated_coord.y, y);
        }
    };

    for (chip_id_t chip : cluster->get_target_device_ids()) {
        check_noc_id_cores(cluster, chip, CoreType::TENSIX);
        check_noc_id_harvested_cores(cluster, chip, CoreType::TENSIX);

        check_noc_id_cores(cluster, chip, CoreType::ETH);
        check_noc_id_harvested_cores(cluster, chip, CoreType::ETH);

        // TODO: figure out how to read this information on Wormhole.
        if (arch == tt::ARCH::BLACKHOLE) {
            check_noc_id_cores(cluster, chip, CoreType::DRAM);
            check_noc_id_harvested_cores(cluster, chip, CoreType::DRAM);
        }

        // TODO: figure out how to read this information on WH and BH.
        // check_noc_id_cores(cluster, chip, CoreType::ARC);

        // TODO: figure out why this hangs the chip both on WH and BH.
        // check_noc_id_cores(cluster, chip, CoreType::PCIE);
    }
}
