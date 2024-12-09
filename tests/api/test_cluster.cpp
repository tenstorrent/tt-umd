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
    if (umd_cluster->get_soc_descriptor(*umd_cluster->get_all_chips_in_cluster().begin()).arch ==
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
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_desc(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0, "LARGE_WRITE_TLB");

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size, "LARGE_READ_TLB");

        // umd_cluster->wait_for_non_mmio_flush(chip_id);

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
        // const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_desc(chip_id);

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

        // umd_cluster->wait_for_non_mmio_flush(chip_id);

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiClusterTest, SimpleIOSpecificChips) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    // TODO: Make this test work on a host system without any tt devices.
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>(0);

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
        // const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_desc(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0, "LARGE_WRITE_TLB");

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_all_chips_in_cluster()) {
        // const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_desc(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size, "LARGE_READ_TLB");

        // umd_cluster->wait_for_non_mmio_flush(chip_id);

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ClusterAPI, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for
    // each transaction

    std::unique_ptr<Cluster> cluster = get_cluster();

    setup_wormhole_remote(cluster.get());

    tt_device_params default_params;
    cluster->start_device(default_params);
    cluster->deassert_risc_reset();

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = zeros;

    static const uint32_t num_loops = 100;

    std::unordered_set<chip_id_t> target_devices = cluster->get_all_chips_in_cluster();
    for (const chip_id_t chip : target_devices) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        // Write to each core a 100 times at different statically mapped addresses
        const tt_SocDescriptor& soc_desc = cluster->get_soc_desc(chip);
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
