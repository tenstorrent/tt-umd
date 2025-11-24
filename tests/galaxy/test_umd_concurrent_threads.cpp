// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <filesystem>
#include <numeric>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "test_galaxy_common.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "tests/wormhole/test_wh_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "wormhole/eth_interface.h"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

// Have 2 threads read and write to all cores on the Galaxy.
TEST(GalaxyConcurrentThreads, WriteToAllChipsL1) {
    auto cluster = std::make_unique<Cluster>();
    if (is_4u_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test on 4U Galaxy due to intermittent failures.";
    }

    // Galaxy Setup.
    std::shared_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();
    std::set<ChipId> target_devices_th1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    std::set<ChipId> target_devices_th2 = {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};

    if (is_4u_galaxy_configuration(cluster.get())) {
        target_devices_th2.insert(32);
    }

    std::unordered_set<ChipId> all_devices = {};
    std::set_union(
        target_devices_th1.begin(),
        target_devices_th1.end(),
        target_devices_th2.begin(),
        target_devices_th2.end(),
        std::inserter(all_devices, all_devices.begin()));
    for (const auto& chip : target_devices_th1) {
        // Verify that selected chips are in the cluster.
        auto it = std::find(cluster_desc->get_all_chips().begin(), cluster_desc->get_all_chips().end(), chip);
        ASSERT_TRUE(it != cluster_desc->get_all_chips().end())
            << "Target chip on thread 1 " << chip << " is not in the Galaxy cluster";
    }
    for (const auto& chip : target_devices_th2) {
        // Verify that selected chips are in the cluster.
        auto it = std::find(cluster_desc->get_all_chips().begin(), cluster_desc->get_all_chips().end(), chip);
        ASSERT_TRUE(it != cluster_desc->get_all_chips().end())
            << "Target chip on thread 2 " << chip << " is not in the Galaxy cluster";
    }

    Cluster device(ClusterOptions{
        .target_devices = all_devices,
    });

    tt::umd::test::utils::set_barrier_params(device);

    DeviceParams default_params;
    device.start_device(default_params);

    // Test.
    std::vector<uint32_t> vector_to_write_th1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> vector_to_write_th2 = {100, 101, 102, 103, 104, 105};

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t write_size = vector_to_write_th1.size() * 4;
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (const auto& chip : target_devices_th1) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                device.write_to_device(
                    vector_to_write_th1.data(),
                    vector_to_write_th1.size() * sizeof(std::uint32_t),
                    chip,
                    core,
                    address);
            }
        }
        device.wait_for_non_mmio_flush();
        for (auto& chip : target_devices_th1) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                test_utils::read_data_from_device(device, readback_vec, chip, core, address, write_size);
                EXPECT_EQ(vector_to_write_th1, readback_vec)
                    << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                readback_vec = {};
            }
        }
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t write_size = vector_to_write_th2.size() * 4;
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (const auto& chip : target_devices_th2) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                device.write_to_device(
                    vector_to_write_th2.data(),
                    vector_to_write_th2.size() * sizeof(std::uint32_t),
                    chip,
                    core,
                    address);
            }
        }
        device.wait_for_non_mmio_flush();
        for (const auto& chip : target_devices_th2) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                test_utils::read_data_from_device(device, readback_vec, chip, core, address, write_size);
                EXPECT_EQ(vector_to_write_th2, readback_vec)
                    << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                readback_vec = {};
            }
        }
    });

    th1.join();
    th2.join();
    device.close_device();
}

TEST(GalaxyConcurrentThreads, WriteToAllChipsDram) {
    auto cluster = std::make_unique<Cluster>();
    if (is_4u_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test on 4U Galaxy due to intermittent failures.";
    }

    // Galaxy Setup.
    std::shared_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();
    std::set<ChipId> target_devices_th1 = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30};
    std::set<ChipId> target_devices_th2 = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31};

    if (is_4u_galaxy_configuration(cluster.get())) {
        target_devices_th2.insert(32);
    }

    std::unordered_set<ChipId> all_devices = {};
    std::set_union(
        std::begin(target_devices_th1),
        std::end(target_devices_th1),
        std::begin(target_devices_th2),
        std::end(target_devices_th2),
        std::inserter(all_devices, std::begin(all_devices)));
    for (const auto& chip : target_devices_th1) {
        // Verify that selected chips are in the cluster.
        auto it = std::find(cluster_desc->get_all_chips().begin(), cluster_desc->get_all_chips().end(), chip);
        ASSERT_TRUE(it != cluster_desc->get_all_chips().end())
            << "Target chip on thread 1 " << chip << " is not in the Galaxy cluster";
    }
    for (const auto& chip : target_devices_th2) {
        // Verify that selected chips are in the cluster.
        auto it = std::find(cluster_desc->get_all_chips().begin(), cluster_desc->get_all_chips().end(), chip);
        ASSERT_TRUE(it != cluster_desc->get_all_chips().end())
            << "Target chip on thread 2 " << chip << " is not in the Galaxy cluster";
    }

    Cluster device(ClusterOptions{
        .target_devices = all_devices,
    });

    tt::umd::test::utils::set_barrier_params(device);

    DeviceParams default_params;
    device.start_device(default_params);

    // Test.
    std::vector<uint32_t> vector_to_write = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    std::uint32_t write_size = vector_to_write.size() * 4;

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x4000000;
        for (const auto& chip : target_devices_th1) {
            for (const CoreCoord& core : device.get_soc_descriptor(0).get_cores(CoreType::DRAM)) {
                device.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip, core, address);
            }
        }
        device.wait_for_non_mmio_flush();
        for (const auto& chip : target_devices_th1) {
            for (const CoreCoord& core : device.get_soc_descriptor(0).get_cores(CoreType::DRAM)) {
                test_utils::read_data_from_device(device, readback_vec, chip, core, address, write_size);
                EXPECT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from dram core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
        }
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x5000000;
        for (const auto& chip : target_devices_th2) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                device.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip, core, address);
            }
        }
        device.wait_for_non_mmio_flush();
        for (const auto& chip : target_devices_th2) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                test_utils::read_data_from_device(device, readback_vec, chip, core, address, write_size);
                EXPECT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from dram core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
        }
    });

    th1.join();
    th2.join();
    device.close_device();
}

TEST(GalaxyConcurrentThreads, PushInputsWhileSignalingCluster) {
    // Galaxy Setup.
    std::shared_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();
    Cluster device;
    std::unordered_set<ChipId> target_devices = cluster_desc->get_all_chips();
    tt::umd::test::utils::set_barrier_params(device);

    DeviceParams default_params;
    device.start_device(default_params);

    // Test.
    std::vector<uint32_t> small_vector = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> large_vector(20000, 0xbeef1234);

    std::thread th1 = std::thread([&] {
        ChipId mmio_chip = cluster_desc->get_chips_with_mmio().begin()->first;
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x0;
        device.write_to_device(
            large_vector.data(),
            large_vector.size() * sizeof(std::uint32_t),
            mmio_chip,
            CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0),
            address);
        test_utils::read_data_from_device(
            device,
            readback_vec,
            mmio_chip,
            CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0),
            address,
            large_vector.size() * 4);
        EXPECT_EQ(large_vector, readback_vec) << "Vector read back from dram core "
                                              << "0-0"
                                              << "does not match what was written";
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (const auto& chip : target_devices) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                device.write_to_device(
                    small_vector.data(), small_vector.size() * sizeof(std::uint32_t), chip, core, address);
            }
        }
        device.wait_for_non_mmio_flush();
        for (const auto& chip : target_devices) {
            for (const CoreCoord& core : device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX)) {
                test_utils::read_data_from_device(device, readback_vec, chip, core, address, small_vector.size() * 4);
                EXPECT_EQ(small_vector, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
        }
    });

    th1.join();
    th2.join();
    device.close_device();
}
