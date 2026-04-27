// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <ios>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/semver.hpp"
#include "wormhole/eth_l1_address_map.h"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

using namespace tt::umd;

constexpr std::uint32_t DRAM_BARRIER_BASE = 0;
static const std::vector<tt_xy_pair> ETH_CORES_TRANSLATION_ON = {
    {{25, 16},
     {18, 16},
     {24, 16},
     {19, 16},
     {23, 16},
     {20, 16},
     {22, 16},
     {21, 16},
     {25, 17},
     {18, 17},
     {24, 17},
     {19, 17},
     {23, 17},
     {20, 17},
     {22, 17},
     {21, 17}}};

static const std::vector<uint32_t> T6_X_TRANSLATED_LOCATIONS = {18, 19, 20, 21, 22, 23, 24, 25};
static const std::vector<uint32_t> T6_Y_TRANSLATED_LOCATIONS = {18, 19, 20, 21, 22, 23, 24, 25, 26, 27};

static void set_barrier_params(Cluster& cluster) {
    // Populate address map and NOC parameters that the driver needs for memory barriers and remote transactions.
    cluster.set_barrier_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, DRAM_BARRIER_BASE});
}

TEST(SiliconDriverWH, OneDramOneTensixNoEthSocDesc) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>(ClusterOptions{
        .sdesc_path = "tests/soc_descs/wormhole_b0_one_dram_one_tensix_no_eth.yaml",
    });
}

TEST(SiliconDriverWH, CreateDestroy) {
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting.
    for (int i = 0; i < 50; i++) {
        Cluster cluster(ClusterOptions{
            .sdesc_path = test_utils::GetSocDescAbsPath("wormhole_b0_1x1.yaml"),
        });
    }
}

TEST(SiliconDriverWH, CustomSocDesc) {
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting.
    Cluster cluster(ClusterOptions{
        .sdesc_path = test_utils::GetSocDescAbsPath("wormhole_b0_1x1.yaml"),
    });
    for (const auto& chip : cluster.get_target_device_ids()) {
        ASSERT_EQ(
            cluster.get_soc_descriptor(chip).get_cores(CoreType::TENSIX).size() +
                cluster.get_soc_descriptor(chip).get_harvested_cores(CoreType::TENSIX).size(),
            1)
            << "Expected 1x1 SOC descriptor to be unmodified by driver";
    }
}

TEST(SiliconDriverWH, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for
    // each transaction
    Cluster cluster;
    set_barrier_params(cluster);

    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = {};

    std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
    // Stress-test TLB stability by exercising one chip 100 times at different statically mapped addresses.
    for (int loop = 0; loop < 100; loop++) {
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            cluster.write_to_device(
                vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
            // Barrier to ensure that all writes over ethernet were commited.
            cluster.wait_for_non_mmio_flush();
            test_utils::read_data_from_device(cluster, readback_vec, chip_id, core, address, 40);
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from core " << core.str() << " does not match what was written";
            cluster.wait_for_non_mmio_flush();
            cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
            cluster.wait_for_non_mmio_flush();
            readback_vec = {};
        }
        address += 0x20;  // Increment by uint32_t size for each write
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, MultiThreadedDevice) {
    // Have 2 threads read and write from a single device concurrently
    // All transactions go through a single Dynamic TLB. We want to make sure this is thread/process safe.
    Cluster cluster;
    set_barrier_params(cluster);

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), 0, core, address);
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 40);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
            address += 0x20;
        }
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x30000000;
        for (const std::vector<CoreCoord>& core_ls : cluster.get_soc_descriptor(0).get_dram_cores()) {
            for (int loop = 0; loop < 100; loop++) {
                for (const CoreCoord& core : core_ls) {
                    cluster.write_to_device(
                        vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), 0, core, address);
                    test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 40);
                    ASSERT_EQ(vector_to_write, readback_vec)
                        << "Vector read back from core " << core.str() << " does not match what was written";
                    readback_vec = {};
                }
                address += 0x20;
            }
        }
    });

    th1.join();
    th2.join();
    cluster.close_device();
}

TEST(SiliconDriverWH, DISABLED_BroadcastWrite) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly.
    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    test_utils::safe_test_cluster_start(&cluster);
    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    std::set<uint32_t> rows_to_exclude = {0, 6};
    std::set<uint32_t> cols_to_exclude = {0, 5};
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {};
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {1, 2, 3, 4, 6, 7, 8, 9};

    for (const auto& size : broadcast_sizes) {
        std::vector<uint32_t> vector_to_write(size);
        std::vector<uint32_t> zeros(size);
        std::vector<uint32_t> readback_vec = {};
        for (int i = 0; i < size; i++) {
            vector_to_write[i] = i;
            zeros[i] = 0;
        }
        // Broadcast to Tensix.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude);
        // Broadcast to DRAM.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                if (rows_to_exclude.find(core.y) != rows_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was broadcasted";
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    address);  // Clear any written data
                readback_vec = {};
            }
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from DRAM core " << chip_id << " " << core.str()
                    << " does not match what was broadcasted " << size;
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    address);  // Clear any written data
                readback_vec = {};
            }
        }
        // Wait for data to be cleared before writing next block.
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, DISABLED_VirtualCoordinateBroadcast) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly.
    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    test_utils::safe_test_cluster_start(&cluster);
    auto eth_version = cluster.get_ethernet_firmware_version();
    bool virtual_bcast_supported = (eth_version >= SemVer(6, 8, 0) || eth_version == SemVer(6, 7, 241)) &&
                                   cluster.get_soc_descriptor(*mmio_devices.begin()).noc_translation_enabled;
    if (!virtual_bcast_supported) {
        cluster.close_device();
        GTEST_SKIP() << "SiliconDriverWH.VirtualCoordinateBroadcast skipped since ethernet version does not support "
                        "Virtual Coordinate Broadcast or NOC translation is not enabled";
    }

    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    std::set<uint32_t> rows_to_exclude = {0, 3, 5, 6, 8, 9};
    std::set<uint32_t> cols_to_exclude = {0, 5};
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {};
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {1, 2, 3, 4, 6, 7, 8, 9};

    for (const auto& size : broadcast_sizes) {
        std::vector<uint32_t> vector_to_write(size);
        std::vector<uint32_t> zeros(size);
        std::vector<uint32_t> readback_vec = {};
        for (int i = 0; i < size; i++) {
            vector_to_write[i] = i;
            zeros[i] = 0;
        }
        // Broadcast to Tensix.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude);
        // Broadcast to DRAM.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                // Rows are excluded according to virtual coordinates, so we have to translate to that system before
                // accessing .y coordinate.
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was broadcasted";
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    address);  // Clear any written data
                readback_vec = {};
            }
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from DRAM core " << chip_id << " " << core.str()
                    << " does not match what was broadcasted " << size;
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    address);  // Clear any written data
                readback_vec = {};
            }
        }
        // Wait for data to be cleared before writing next block.
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

/**
 * Test the PCIe DMA controller by using it to write and read fixed-size patterns
 * to and from a single ETH core at device address 254304 (0x3E160), then verify
 * that the data read back matches what was written.
 */
TEST(TestDeviceIO, DMA3) {
    const ChipId chip = 0;
    Cluster cluster;

    CoreCoord eth_core = cluster.get_soc_descriptor(chip).get_cores(CoreType::ETH)[0];

    size_t buf_size = 768;

    std::vector<uint8_t> zeros(buf_size, 1);
    cluster.write_to_device(zeros.data(), zeros.size(), chip, eth_core, 254304);
    std::vector<uint8_t> readback_zeros(buf_size, 0xFF);
    cluster.read_from_device(readback_zeros.data(), chip, eth_core, 254304, readback_zeros.size());
    EXPECT_EQ(zeros, readback_zeros) << "Mismatch zeros for core " << eth_core.str() << " addr=0x0"
                                     << " size=" << std::dec << readback_zeros.size();

    std::vector<uint8_t> pattern(buf_size, 0);
    for (size_t i = 0; i < buf_size; ++i) {
        pattern[i] = i % 256;
    }

    cluster.dma_write_to_device(pattern.data(), pattern.size(), chip, eth_core, 254304);

    std::vector<uint8_t> readback(buf_size, 1);
    cluster.read_from_device(readback.data(), chip, eth_core, 254304, readback.size());

    EXPECT_EQ(pattern, readback) << "Mismatch for core " << eth_core.str() << " addr=0x0"
                                 << " size=" << std::dec << readback.size();
}
