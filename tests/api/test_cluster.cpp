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
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/mock_chip.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"

// TODO: obviously we need some other way to set this up
#include "noc/noc_parameters.h"
#include "src/firmware/riscv/wormhole/eth_l1_address_map.h"
#include "src/firmware/riscv/wormhole/host_mem_address_map.h"
#include "src/firmware/riscv/wormhole/l1_address_map.h"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// E75, E150, E300
// N150. N300
// Galaxy

inline std::unique_ptr<Cluster> get_cluster() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        return nullptr;
    }
    return std::unique_ptr<Cluster>(new Cluster());
}

// TODO: Should not be wormhole specific.
// TODO: Offer default setup for what you can.
void setup_wormhole_remote(Cluster* umd_cluster) {
    if (!umd_cluster->get_target_remote_device_ids().empty() &&
        umd_cluster->get_soc_descriptor(*umd_cluster->get_all_chips_in_cluster().begin()).arch ==
            tt::ARCH::WORMHOLE_B0) {
        // Populate address map and NOC parameters that the driver needs for remote transactions

        umd_cluster->set_device_l1_address_params(
            {l1_mem::address_map::L1_BARRIER_BASE,
             eth_l1_mem::address_map::ERISC_BARRIER_BASE,
             eth_l1_mem::address_map::FW_VERSION_ADDR});
    }
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
    std::string sdesc_path = tt_SocDescriptor::get_soc_descriptor_path(device_arch);
    umd_cluster = std::make_unique<Cluster>(sdesc_path, target_devices);
    umd_cluster = nullptr;

    // 4. Constructor for creating a cluster with mock chip.
    umd_cluster = Cluster::create_mock_cluster();
    umd_cluster = nullptr;
}

TEST(ApiClusterTest, SimpleIOAllChips) {
    std::unique_ptr<Cluster> umd_cluster = get_cluster();

    if (umd_cluster == nullptr || umd_cluster->get_all_chips_in_cluster().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    // TODO: this should be part of constructor if it is mandatory.
    setup_wormhole_remote(umd_cluster.get());

    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global(chip_id, any_core);

        if (cluster_desc->is_chip_remote(chip_id) && soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, any_core_global, 0, "LARGE_WRITE_TLB");
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global(chip_id, any_core);

        if (cluster_desc->is_chip_remote(chip_id) && soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), any_core_global, 0, data_size, "LARGE_READ_TLB");

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiClusterTest, RemoteFlush) {
    std::unique_ptr<Cluster> umd_cluster = get_cluster();

    if (umd_cluster == nullptr || umd_cluster->get_all_chips_in_cluster().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);

    // TODO: this should be part of constructor if it is mandatory.
    setup_wormhole_remote(umd_cluster.get());

    for (auto chip_id : umd_cluster->get_target_remote_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global(chip_id, any_core);

        if (!cluster_desc->is_chip_remote(chip_id)) {
            std::cout << "Chip " << chip_id << " skipped because it is not a remote chip." << std::endl;
            continue;
        }

        if (soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;
        umd_cluster->write_to_device(data.data(), data_size, any_core_global, 0, "LARGE_WRITE_TLB");

        std::cout << "Waiting for remote chip flush " << chip_id << std::endl;
        umd_cluster->wait_for_non_mmio_flush(chip_id);

        std::cout << "Waiting again for flush " << chip_id << ", should be no-op" << std::endl;
        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    chip_id_t any_remote_chip = *umd_cluster->get_target_remote_device_ids().begin();
    const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(any_remote_chip);
    tt_xy_pair any_core = soc_desc.workers[0];
    tt_cxy_pair any_core_global(any_remote_chip, any_core);
    if (soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
        std::cout << "Skipping whole cluster wait because it is not a wormhole_b0 chip." << std::endl;
        return;
    }
    std::cout << "Writing to chip " << any_remote_chip << " core " << any_core.str() << std::endl;
    umd_cluster->write_to_device(data.data(), data_size, any_core_global, 0, "LARGE_WRITE_TLB");

    std::cout << "Testing whole cluster wait for remote chip flush." << std::endl;
    umd_cluster->wait_for_non_mmio_flush();

    std::cout << "Testing whole cluster wait for remote chip flush again, should be no-op." << std::endl;
    umd_cluster->wait_for_non_mmio_flush();
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

    // TODO: this should be part of constructor if it is mandatory.
    setup_wormhole_remote(umd_cluster.get());

    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global(chip_id, any_core);

        if (cluster_desc->is_chip_remote(chip_id) && soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, any_core_global, 0, "LARGE_WRITE_TLB");
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        // TODO: figure out if core locations should contain chip_id
        tt_xy_pair any_core = soc_desc.workers[0];
        tt_cxy_pair any_core_global(chip_id, any_core);

        if (cluster_desc->is_chip_remote(chip_id) && soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), any_core_global, 0, data_size, "LARGE_READ_TLB");

        ASSERT_EQ(data, readback_data);
    }
}
