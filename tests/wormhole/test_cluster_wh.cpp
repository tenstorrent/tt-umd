// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/utils/semver.hpp"
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
    DeviceParams default_params;
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting
    for (int i = 0; i < 50; i++) {
        Cluster cluster(ClusterOptions{
            .perform_harvesting = false,
            .sdesc_path = test_utils::GetSocDescAbsPath("wormhole_b0_1x1.yaml"),
        });
        set_barrier_params(cluster);

        // TODO: this test fails on new UBB galaxy if the two lines are uncommented.
        // Generally we don't want to call start_device and close_device in tests.
        // Implement loading ebreak code before each test.
        // cluster.start_device(default_params);
        // cluster.close_device();
    }
}

TEST(SiliconDriverWH, CustomSocDesc) {
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting
    Cluster cluster(ClusterOptions{
        .perform_harvesting = false,
        .simulated_harvesting_masks = {60, 0, 0},
        .simulated_harvesting_masks_per_chip = {{0, {30, 0, 0}}, {1, {60, 0, 0}}},
        .sdesc_path = test_utils::GetSocDescAbsPath("wormhole_b0_1x1.yaml"),
    });
    for (const auto& chip : cluster.get_target_device_ids()) {
        ASSERT_EQ(cluster.get_soc_descriptor(chip).get_cores(CoreType::TENSIX).size(), 1)
            << "Expected 1x1 SOC descriptor to be unmodified by driver";
    }
}

TEST(SiliconDriverWH, HarvestingRuntime) {
    Cluster cluster(ClusterOptions{
        .simulated_harvesting_masks = {60, 0, 0},
        .simulated_harvesting_masks_per_chip = {{0, {30, 0, 0}}, {1, {60, 0, 0}}},
    });
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores
    for (auto chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
            cluster.configure_tlb(chip_id, core, 1 << 20, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
        }
    }

    DeviceParams default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> dynamic_readback_vec = {};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    for (auto chip_id : cluster.get_target_device_ids()) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        std::uint32_t dynamic_write_address = 0x40000000;
        for (int loop = 0; loop < 100;
             loop++) {  // Write to each core a 100 times at different statically mapped addresses
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
                cluster.write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    dynamic_write_address);
                cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

                test_utils::read_data_from_device(cluster, readback_vec, chip_id, core, address, 40);
                test_utils::read_data_from_device(
                    cluster, dynamic_readback_vec, chip_id, core, dynamic_write_address, 40);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                ASSERT_EQ(vector_to_write, dynamic_readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                cluster.wait_for_non_mmio_flush();

                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    dynamic_write_address);  // Clear any written data
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    address);  // Clear any written data
                cluster.wait_for_non_mmio_flush();
                readback_vec = {};
                dynamic_readback_vec = {};
            }
            address += 0x20;  // Increment by uint32_t size for each write
            dynamic_write_address += 0x20;
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, UnalignedStaticTLB_RW) {
    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores
    for (auto chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
            cluster.configure_tlb(chip_id, core, 1 << 20, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
        }
    }

    DeviceParams default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> unaligned_sizes = {3, 14, 21, 255, 362, 430, 1022, 1023, 1025};
    for (auto chip_id : cluster.get_target_device_ids()) {
        for (const auto& size : unaligned_sizes) {
            std::vector<uint8_t> write_vec(size, 0);
            for (int i = 0; i < size; i++) {
                write_vec[i] = size + i;
            }
            std::vector<uint8_t> readback_vec(size, 0);
            std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
            for (int loop = 0; loop < 50; loop++) {
                for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                    cluster.write_to_device(write_vec.data(), size, chip_id, core, address);
                    cluster.wait_for_non_mmio_flush();
                    cluster.read_from_device(readback_vec.data(), chip_id, core, address, size);
                    ASSERT_EQ(readback_vec, write_vec);
                    readback_vec = std::vector<uint8_t>(size, 0);
                    cluster.write_to_sysmem(write_vec.data(), size, 0, 0, 0);
                    cluster.read_from_sysmem(readback_vec.data(), 0, 0, size, 0);
                    ASSERT_EQ(readback_vec, write_vec);
                    readback_vec = std::vector<uint8_t>(size, 0);
                    cluster.wait_for_non_mmio_flush();
                }
                address += 0x20;
            }
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, StaticTLB_RW) {
    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores
    for (auto chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
            cluster.configure_tlb(chip_id, core, 1 << 20, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
        }
    }

    DeviceParams default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space
    for (auto chip_id : cluster.get_target_device_ids()) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        // Write to each core a 100 times at different statically mapped addresses
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
                // Barrier to ensure that all writes over ethernet were commited
                cluster.wait_for_non_mmio_flush();
                test_utils::read_data_from_device(cluster, readback_vec, chip_id, core, address, 40);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                cluster.wait_for_non_mmio_flush();
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    address);  // Clear any written data
                cluster.wait_for_non_mmio_flush();
                readback_vec = {};
            }
            address += 0x20;  // Increment by uint32_t size for each write
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for
    // each transaction
    Cluster cluster;

    set_barrier_params(cluster);

    DeviceParams default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = {};

    for (auto chip_id : cluster.get_target_device_ids()) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        // Write to each core a 100 times at different statically mapped addresses
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
                // Barrier to ensure that all writes over ethernet were commited
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
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, MultiThreadedDevice) {
    // Have 2 threads read and write from a single device concurrently
    // All transactions go through a single Dynamic TLB. We want to make sure this is thread/process safe
    Cluster cluster;
    set_barrier_params(cluster);

    DeviceParams default_params;
    cluster.start_device(default_params);

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

TEST(SiliconDriverWH, MultiThreadedMemBar) {
    // Have 2 threads read and write from a single device concurrently
    // All (fairly large) transactions go through a static TLB.
    // We want to make sure the memory barrier is thread/process safe.

    // Memory barrier flags get sent to address 0 for all channels in this test
    uint32_t base_addr = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;

    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores
    for (auto chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE.
            cluster.configure_tlb(chip_id, core, 1 << 20, base_addr);
        }
    }

    DeviceParams default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> readback_membar_vec = {};
    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, l1_mem::address_map::L1_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers were correctly initialized on all workers
        readback_membar_vec = {};
    }

    for (int chan = 0; chan < cluster.get_soc_descriptor(0).get_num_dram_channels(); chan++) {
        CoreCoord core = cluster.get_soc_descriptor(0).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
        test_utils::read_data_from_device(cluster, readback_membar_vec, 0, core, 0, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers were correctly initialized on all DRAM
        readback_membar_vec = {};
    }

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::ETH)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0),
            187);  // Ensure that memory barriers were correctly initialized on all ethernet cores
        readback_membar_vec = {};
    }

    // Launch 2 thread accessing different locations of L1 and using memory barrier between write and read
    // Ensure now RAW race and membars are thread safe
    std::vector<uint32_t> vec1(2560);
    std::vector<uint32_t> vec2(2560);
    std::vector<uint32_t> zeros(2560, 0);

    for (int i = 0; i < vec1.size(); i++) {
        vec1.at(i) = i;
    }
    for (int i = 0; i < vec2.size(); i++) {
        vec2.at(i) = vec1.size() + i;
    }
    std::thread th1 = std::thread([&] {
        std::uint32_t address = base_addr;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                std::vector<uint32_t> readback_vec = {};
                cluster.write_to_device(vec1.data(), vec1.size() * sizeof(std::uint32_t), 0, core, address);
                cluster.l1_membar(0, {core});
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 4 * vec1.size());
                ASSERT_EQ(readback_vec, vec1);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), 0, core, address);
                readback_vec = {};
            }
        }
    });

    std::thread th2 = std::thread([&] {
        std::uint32_t address = base_addr + vec1.size() * 4;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                std::vector<uint32_t> readback_vec = {};
                cluster.write_to_device(vec2.data(), vec2.size() * sizeof(std::uint32_t), 0, core, address);
                cluster.l1_membar(0, {core});
                test_utils::read_data_from_device(cluster, readback_vec, 0, core, address, 4 * vec2.size());
                ASSERT_EQ(readback_vec, vec2);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), 0, core, address);
                readback_vec = {};
            }
        }
    });

    th1.join();
    th2.join();

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, l1_mem::address_map::L1_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0), 187);  // Ensure that memory barriers end up in the correct sate for workers
        readback_membar_vec = {};
    }

    for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::ETH)) {
        test_utils::read_data_from_device(
            cluster, readback_membar_vec, 0, core, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 4);
        ASSERT_EQ(
            readback_membar_vec.at(0),
            187);  // Ensure that memory barriers end up in the correct sate for ethernet cores
        readback_membar_vec = {};
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, DISABLED_BroadcastWrite) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly
    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    DeviceParams default_params;
    cluster.start_device(default_params);
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
        // Broadcast to Tensix
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude);
        // Broadcast to DRAM
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
        // Wait for data to be cleared before writing next block
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, DISABLED_VirtualCoordinateBroadcast) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly
    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    DeviceParams default_params;
    cluster.start_device(default_params);
    auto eth_version = cluster.get_ethernet_firmware_version();
    bool virtual_bcast_supported = (eth_version >= semver_t(6, 8, 0) || eth_version == semver_t(6, 7, 241)) &&
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
        // Broadcast to Tensix
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude);
        // Broadcast to DRAM
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
        // Wait for data to be cleared before writing next block
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, LargeAddressTlb) {
    Cluster cluster;

    const CoreCoord ARC_CORE = cluster.get_soc_descriptor(0).get_cores(CoreType::ARC).at(0);

    set_barrier_params(cluster);
    cluster.start_device({});

    // Address of the reset unit in ARC core:
    uint64_t arc_reset_noc = 0x880030000ULL;

    // Offset to the scratch registers in the reset unit:
    uint64_t scratch_offset = 0x60;

    // Map a TLB to the reset unit in ARC core:
    cluster.configure_tlb(0, ARC_CORE, 1 << 20, arc_reset_noc);

    // Address of the scratch register in the reset unit:
    uint64_t addr = arc_reset_noc + scratch_offset;

    uint32_t value0 = 0;
    uint32_t value1 = 0;
    uint32_t value2 = 0;

    // Read the scratch register via BAR0:
    value0 = cluster.get_chip(0)->get_tt_device()->bar_read32(0x1ff30060);

    // Read the scratch register via the TLB:
    cluster.read_from_device(&value1, 0, ARC_CORE, addr, sizeof(uint32_t));

    // Read the scratch register via a different TLB, different code path:
    cluster.read_from_device(&value2, 0, ARC_CORE, addr, sizeof(uint32_t));

    // Mask off lower 16 bits; FW changes these dynamically:
    value0 &= 0xffff0000;
    value1 &= 0xffff0000;
    value2 &= 0xffff0000;

    // Check that the values are the same:
    EXPECT_EQ(value1, value0);
    EXPECT_EQ(value2, value0);
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size patterns
 * to 0x0 in several DRAM cores, then reading them back and verifying.
 */
TEST(SiliconDriverWH, DMA1) {
    const ChipId chip = 0;
    Cluster cluster;

    auto& soc_descriptor = cluster.get_soc_descriptor(chip);
    size_t dram_count = soc_descriptor.get_num_dram_channels();
    std::vector<CoreCoord> dram_cores;
    for (size_t i = 0; i < dram_count; ++i) {
        dram_cores.push_back(soc_descriptor.get_dram_core_for_channel(i, 0, CoordSystem::NOC0));
    }

    // 16.5 MiB: Larger than the largest WH TLB window; this forces chunking
    // and TLB reassignment.
    size_t buf_size = 0x1080000;

    // Keep track of the patterns we wrote to DRAM so we can verify them later.
    std::vector<std::vector<uint8_t>> patterns;

    // First, write a different pattern to each of the DRAM cores.
    for (auto core : dram_cores) {
        std::vector<uint8_t> pattern(buf_size);
        test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

        cluster.dma_write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);

        patterns.push_back(pattern);
    }

    // Now, read back the patterns we wrote to DRAM and verify them.
    for (size_t i = 0; i < dram_cores.size(); ++i) {
        auto core = dram_cores[i];
        std::vector<uint8_t> readback(buf_size, 0x0);

        cluster.dma_read_from_device(readback.data(), readback.size(), chip, core, 0x0);

        EXPECT_EQ(patterns[i], readback) << "Mismatch for core " << core.str() << " addr=0x0"
                                         << " size=" << std::dec << readback.size();
    }
}

/**
 * Test the PCIe DMA controller by using it to write random patterns of random
 * sizes to random addresses in several DRAM cores, then reading them back and
 * verifying.  Addresses are constrained to be within the first 1 GiB of each
 * DRAM channel and are aligned to 4 bytes.  Sizes are constrained to be between
 * 4 bytes and 32 MiB, and are aligned to 4 bytes.  Also tested is the case
 * where the write is done using MMIO instead of DMA.
 */
TEST(SiliconDriverWH, DMA2) {
    const ChipId chip = 0;
    Cluster cluster;

    auto& soc_descriptor = cluster.get_soc_descriptor(chip);
    size_t dram_count = soc_descriptor.get_num_dram_channels();
    std::vector<CoreCoord> dram_cores;
    for (size_t i = 0; i < dram_count; ++i) {
        dram_cores.push_back(soc_descriptor.get_dram_core_for_channel(i, 0, CoordSystem::NOC0));
    }

    // Constraints for random address/size generation.
    const size_t MIN_BUF_SIZE = 4;
    const size_t MAX_BUF_SIZE = 0x2000000;
    const uint64_t MIN_ADDR = 0x0;
    const uint64_t MAX_ADDR = 0x3e000000;

    // Setup random number generation.
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> size_dist(MIN_BUF_SIZE, MAX_BUF_SIZE);
    std::uniform_int_distribution<uint64_t> addr_dist(MIN_ADDR, MAX_ADDR);

    // Structure to keep track of the operations.
    struct DmaOpInfo {
        CoreCoord core;
        uint64_t address;
        std::vector<uint8_t> data;  // Store the actual data written for verification.
    };

    const size_t ITERATIONS = 25;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        std::vector<DmaOpInfo> write_ops;
        write_ops.reserve(dram_cores.size());

        // First, write a different random pattern to a random address on each DRAM core.
        for (const auto& core : dram_cores) {
            // Generate random size and address.
            size_t size = size_dist(rng) & ~0x3ULL;
            uint64_t addr = 0;

            // Generate a random pattern of the specified size.
            std::vector<uint8_t> pattern(size);
            test_utils::fill_with_random_bytes(pattern.data(), pattern.size());

            // Perform the DMA write.
            cluster.dma_write_to_device(pattern.data(), pattern.size(), chip, core, addr);

            // Store the operation details for verification.
            write_ops.push_back({core, addr, pattern});
        }

        // Now, read back the patterns we wrote to DRAM and verify them.
        for (const auto& op : write_ops) {
            std::vector<uint8_t> readback(op.data.size());

            // Perform the DMA read from the specific address.
            cluster.dma_read_from_device(readback.data(), readback.size(), chip, op.core, op.address);

            // Verify the data.
            EXPECT_EQ(op.data, readback) << "Mismatch for core " << op.core.str() << " addr=0x" << std::hex
                                         << op.address << " size=" << std::dec << op.data.size();
        }
    }

    // Do it again but use MMIO writes to the DRAM cores instead of DMA.
    // DMA is still used for readback.  The inverse of this test (DMA for write;
    // MMIO for read) is omitted because of how slow MMIO reads are.
    for (size_t i = 0; i < ITERATIONS; ++i) {
        std::vector<DmaOpInfo> write_ops;
        write_ops.reserve(dram_cores.size());

        // First, write a different random pattern to a random address on each DRAM core.
        for (const auto& dram_core : dram_cores) {
            // Generate random size and address.
            size_t size = size_dist(rng) & ~0x3ULL;
            uint64_t addr = 0;

            // Generate a random pattern of the specified size.
            std::vector<uint8_t> pattern(size);
            test_utils::fill_with_random_bytes(pattern.data(), pattern.size());

            // Perform the DMA write.
            cluster.write_to_device(pattern.data(), pattern.size(), chip, dram_core, addr);

            // Store the operation details for verification.
            write_ops.push_back({dram_core, addr, pattern});
        }

        // Now, read back the patterns we wrote to DRAM and verify them.
        for (const auto& op : write_ops) {
            std::vector<uint8_t> readback(op.data.size());

            // Perform the DMA read from the specific address.
            cluster.dma_read_from_device(readback.data(), readback.size(), chip, op.core, op.address);

            // Verify the data.
            EXPECT_EQ(op.data, readback) << "Mismatch for core " << op.core.str() << " addr=0x" << std::hex
                                         << op.address << " size=" << std::dec << op.data.size();
        }
    }
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size patterns
 * to 0x0 in several DRAM cores, then reading them back and verifying.
 */
TEST(SiliconDriverWH, DMA3) {
    const ChipId chip = 0;
    Cluster cluster;

    CoreCoord eth_core = CoreCoord(21, 17, CoreType::ETH, CoordSystem::TRANSLATED);

    size_t buf_size = 768;

    std::vector<uint8_t> zeros(buf_size, 1);
    cluster.write_to_device(zeros.data(), zeros.size(), chip, eth_core, 254304);
    std::vector<uint8_t> readback_zeros(buf_size, 0xFF);
    cluster.read_from_device(readback_zeros.data(), chip, eth_core, 254304, readback_zeros.size());
    EXPECT_EQ(zeros, readback_zeros) << "Mismatch zeros or core " << eth_core.str() << " addr=0x0"
                                     << " size=" << std::dec << readback_zeros.size();

    std::vector<uint8_t> pattern(buf_size, 0);
    for (int i = 0; i < buf_size; ++i) {
        pattern[i] = i % 256;
    }

    cluster.dma_write_to_device(pattern.data(), pattern.size(), chip, eth_core, 254304);

    std::vector<uint8_t> readback(buf_size, 1);
    cluster.read_from_device(readback.data(), chip, eth_core, 254304, readback.size());

    EXPECT_EQ(pattern, readback) << "Mismatch for core " << eth_core.str() << " addr=0x0"
                                 << " size=" << std::dec << readback.size();
}
