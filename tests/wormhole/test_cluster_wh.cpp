// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ios>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip/chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/ethernet_broadcast.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"
#include "wormhole/eth_l1_address_map.h"
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
            cluster.get_tt_device(chip)->get_soc_descriptor().get_cores(CoreType::TENSIX).size() +
                cluster.get_tt_device(chip)->get_soc_descriptor().get_harvested_cores(CoreType::TENSIX).size(),
            1)
            << "Expected 1x1 SOC descriptor to be used in TTDevice.";
        ASSERT_EQ(
            cluster.get_soc_descriptor(chip).get_cores(CoreType::TENSIX).size() +
                cluster.get_soc_descriptor(chip).get_harvested_cores(CoreType::TENSIX).size(),
            1)
            << "Expected 1x1 SOC descriptor to be unmodified by driver.";
    }
}

TEST(SiliconDriverWH, HarvestingRuntime) {
    Cluster cluster(ClusterOptions{});
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores.
    for (auto chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
            cluster.configure_tlb(
                chip_id, core, tt::umd::wormhole::STATIC_TLB_SIZE, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
        }
    }

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
    Cluster cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    set_barrier_params(cluster);

    // Do this only for a single chip to speed up the test.
    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);
    for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
        // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
        cluster.configure_tlb(
            chip_id, core, tt::umd::wormhole::STATIC_TLB_SIZE, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
    }

    test_utils::safe_test_cluster_start(&cluster);

    std::vector<uint32_t> unaligned_sizes = {3, 14, 21, 255, 362, 430, 1022, 1023, 1025};
    for (const auto& size : unaligned_sizes) {
        std::vector<uint8_t> write_vec(size, 0);
        std::iota(write_vec.begin(), write_vec.end(), static_cast<uint8_t>(size));
        std::vector<uint8_t> readback_vec(size, 0);
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (int loop = 0; loop < 50; loop++) {
            for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
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
    cluster.close_device();
}

TEST(SiliconDriverWH, StaticTLB_RW) {
    Cluster cluster;
    set_barrier_params(cluster);

    // Do this only for a single chip to speed up the test.
    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);
    for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
        // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
        cluster.configure_tlb(
            chip_id, core, tt::umd::wormhole::STATIC_TLB_SIZE, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
    }

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space.
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
    cluster.close_device();
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

// TODO(#2485): Re-enable. Writes and reads are not synchronized so they can land on the device out of order; broke
// after PR #2455.
TEST(SiliconDriverWH, DISABLED_MultiThreadedDevice) {
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

TEST(SiliconDriverWH, MultiThreadedMemBar) {
    // Have 2 threads read and write from a single device concurrently
    // All (fairly large) transactions go through a static TLB.
    // We want to make sure the memory barrier is thread/process safe.

    // Memory barrier flags get sent to address 0 for all channels in this test.
    uint32_t base_addr = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;

    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores.
    for (auto chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE.
            cluster.configure_tlb(chip_id, core, tt::umd::wormhole::STATIC_TLB_SIZE, base_addr);
        }
    }

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
    // Ensure now RAW race and membars are thread safe.
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

TEST(SiliconDriverWH, BroadcastWrite) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly, and that
    // a broadcast targeting one core type does not leak writes to the other.
    Cluster cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    set_barrier_params(cluster);

    test_utils::safe_test_cluster_start(&cluster);
    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    // This excludes DRAM and ETH banks in noc0 coords.
    std::set<uint32_t> rows_to_exclude = {0, 6};
    std::set<uint32_t> cols_to_exclude = {0, 5};
    // This excludes all tensix columns in noc0 coords.
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {};
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {1, 2, 3, 4, 6, 7, 8, 9};

    // Pre-zero tensix L1 and DRAM at the test address so the "not written" assertions have a known baseline.
    std::vector<uint32_t> initial_zeros(broadcast_sizes.back(), 0);
    for (auto chip_id : cluster.get_target_device_ids()) {
        for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
        for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
            const CoreCoord core =
                cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
    }
    cluster.wait_for_non_mmio_flush();

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
            vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude, false);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            // Tensix cores received the broadcast; zero them out.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                if (rows_to_exclude.find(core.y) != rows_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from chip " << chip_id << " core " << core.str()
                    << " does not match what was broadcasted for size " << size;
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
                readback_vec = {};
            }
            // DRAM cores must NOT have been written by the tensix broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "DRAM core " << chip_id << " " << core.str()
                                               << " was modified by tensix broadcast for size " << size;
                readback_vec = {};
            }
        }
        cluster.wait_for_non_mmio_flush();

        // Broadcast to DRAM.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast,
            false);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            // DRAM cores received the broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from DRAM core " << chip_id << " " << core.str()
                    << " does not match what was broadcasted for size " << size;
                readback_vec = {};
            }
            // Tensix cores must NOT have been written by the DRAM broadcast.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                if (rows_to_exclude.find(core.y) != rows_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "Tensix core " << chip_id << " " << core.str()
                                               << " was modified by DRAM broadcast for size " << size;
                readback_vec = {};
            }
            // Zero DRAM so the next iteration starts clean.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
            }
        }
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, VirtualCoordinateBroadcast) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly, and that
    // a broadcast targeting one core type does not leak writes to the other.
    Cluster cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    test_utils::safe_test_cluster_start(&cluster);

    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    // This excludes DRAM and ETH banks (positioned in 16, 17 on both rows and columns) and some tensix rows and columns
    // in translated space.
    std::set<uint32_t> rows_to_exclude = {16, 17, 20, 22, 26, 27};
    std::set<uint32_t> cols_to_exclude = {16, 17, 20};
    // This excludes all tensix columns 18-25 in translated space.
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {};
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {18, 19, 20, 21, 22, 23, 24, 25};

    // Pre-zero tensix L1 and DRAM at the test address so the "not written" assertions have a known baseline.
    std::vector<uint32_t> initial_zeros(broadcast_sizes.back(), 0);
    for (auto chip_id : cluster.get_target_device_ids()) {
        for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
        for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
            const CoreCoord core =
                cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
    }
    cluster.wait_for_non_mmio_flush();

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
            vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude, true);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            // Tensix cores received the broadcast; zero them out.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                if (cols_to_exclude.find(translated_core.x) != cols_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from chip " << chip_id << " core " << core.str()
                    << " does not match what was broadcasted for size " << size;
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
                readback_vec = {};
            }
            // DRAM cores must NOT have been written by the tensix broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "DRAM core " << chip_id << " " << core.str()
                                               << " was modified by tensix broadcast for size " << size;
                readback_vec = {};
            }
        }
        cluster.wait_for_non_mmio_flush();

        // Broadcast to DRAM.
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast,
            true);
        cluster.wait_for_non_mmio_flush();

        for (auto chip_id : cluster.get_target_device_ids()) {
            // DRAM cores received the broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from DRAM core " << chip_id << " " << core.str()
                    << " does not match what was broadcasted for size " << size;
                readback_vec = {};
            }
            // Tensix cores must NOT have been written by the DRAM broadcast.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                if (cols_to_exclude.find(translated_core.x) != cols_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "Tensix core " << chip_id << " " << core.str()
                                               << " was modified by DRAM broadcast for size " << size;
                readback_vec = {};
            }
            // Zero DRAM so the next iteration starts clean.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
            }
        }
        cluster.wait_for_non_mmio_flush();
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, VirtualCoordinateBroadcastPerChip) {
    // Broadcast multiple vectors to tensix and dram grid. Verify broadcasted data is read back correctly, and that
    // a broadcast targeting one core type does not leak writes to the other.
    Cluster cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    test_utils::safe_test_cluster_start(&cluster);

    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    // This excludes DRAM and ETH banks (positioned in 16, 17 on both rows and columns) and some tensix rows and columns
    // in translated space.
    std::set<uint32_t> rows_to_exclude = {16, 17, 20, 22, 26, 27};
    std::set<uint32_t> cols_to_exclude = {16, 17, 20};
    // This excludes all tensix columns 18-25 in translated space.
    std::set<uint32_t> rows_to_exclude_for_dram_broadcast = {};
    std::set<uint32_t> cols_to_exclude_for_dram_broadcast = {18, 19, 20, 21, 22, 23, 24, 25};

    // Pre-zero tensix L1 and DRAM on every chip so the "not written" assertions have a known baseline.
    std::vector<uint32_t> initial_zeros(broadcast_sizes.back(), 0);
    for (auto chip_id : cluster.get_target_device_ids()) {
        for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
        for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
            const CoreCoord core =
                cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
            cluster.write_to_device(
                initial_zeros.data(), initial_zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
        }
    }
    cluster.wait_for_non_mmio_flush();

    for (auto chip_id : cluster.get_target_device_ids()) {
        for (const auto& size : broadcast_sizes) {
            std::vector<uint32_t> vector_to_write(size);
            std::vector<uint32_t> zeros(size);
            std::vector<uint32_t> readback_vec = {};
            for (int i = 0; i < size; i++) {
                vector_to_write[i] = i;
                zeros[i] = 0;
            }

            std::set<ChipId> chips_to_exclude = cluster.get_target_device_ids();
            chips_to_exclude.erase(chip_id);

            // Broadcast to Tensix.
            cluster.broadcast_write_to_cluster(
                vector_to_write.data(),
                vector_to_write.size() * 4,
                address,
                chips_to_exclude,
                rows_to_exclude,
                cols_to_exclude,
                true);
            cluster.wait_for_non_mmio_flush();

            // Tensix cores received the broadcast; zero them out.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                if (cols_to_exclude.find(translated_core.x) != cols_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from chip " << chip_id << " core " << core.str()
                    << " does not match what was broadcasted for size " << size;
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
                readback_vec = {};
            }
            // DRAM cores must NOT have been written by the tensix broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "DRAM core " << chip_id << " " << core.str()
                                               << " was modified by tensix broadcast for size " << size;
                readback_vec = {};
            }
            cluster.wait_for_non_mmio_flush();

            // Broadcast to DRAM.
            cluster.broadcast_write_to_cluster(
                vector_to_write.data(),
                vector_to_write.size() * 4,
                address,
                chips_to_exclude,
                rows_to_exclude_for_dram_broadcast,
                cols_to_exclude_for_dram_broadcast,
                true);
            cluster.wait_for_non_mmio_flush();

            // DRAM cores received the broadcast.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from DRAM core " << chip_id << " " << core.str()
                    << " does not match what was broadcasted for size " << size;
                readback_vec = {};
            }
            // Tensix cores must NOT have been written by the DRAM broadcast.
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                if (cols_to_exclude.find(translated_core.x) != cols_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * 4);
                ASSERT_EQ(zeros, readback_vec) << "Tensix core " << chip_id << " " << core.str()
                                               << " was modified by DRAM broadcast for size " << size;
                readback_vec = {};
            }
            // Zero DRAM so the next iteration starts clean.
            for (int chan = 0; chan < cluster.get_soc_descriptor(chip_id).get_num_dram_channels(); chan++) {
                const CoreCoord core =
                    cluster.get_soc_descriptor(chip_id).get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED);
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, core, address);
            }
            cluster.wait_for_non_mmio_flush();
        }
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, EthernetBroadcastSingleRemotePerChip) {
    // For each remote chip, broadcast a vector to its tensix grid using EthernetBroadcastSingleRemote
    // and verify the data is read back correctly.
    Cluster cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    test_utils::safe_test_cluster_start(&cluster);

    auto remote_devices = cluster.get_target_remote_device_ids();
    if (remote_devices.empty()) {
        cluster.close_device();
        GTEST_SKIP() << "SiliconDriverWH.EthernetBroadcastSingleRemotePerChip skipped: no remote devices found";
    }

    auto eth_version = cluster.get_ethernet_firmware_version();
    bool virtual_bcast_supported = (eth_version >= SemVer(6, 8, 0) || eth_version == SemVer(6, 7, 241)) &&
                                   cluster.get_soc_descriptor(*mmio_devices.begin()).noc_translation_enabled;
    if (!virtual_bcast_supported) {
        cluster.close_device();
        GTEST_SKIP() << "SiliconDriverWH.EthernetBroadcastSingleRemotePerChip skipped: ethernet version does not "
                        "support Virtual Coordinate Broadcast or NOC translation is not enabled";
    }

    std::vector<uint32_t> broadcast_sizes = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    // This excludes DRAM and ETH banks (positioned in 16, 17 on both rows and columns) and some tensix rows and columns
    // in translated space.
    std::set<uint32_t> rows_to_exclude = {16, 17, 20, 22, 26, 27};
    std::set<uint32_t> cols_to_exclude = {16, 17, 20};

    for (auto chip_id : remote_devices) {
        RemoteChip* remote_chip = cluster.get_remote_chip(chip_id);
        EthernetBroadcast eth_broadcast(remote_chip->get_remote_communication());

        for (const auto& size : broadcast_sizes) {
            std::vector<uint32_t> vector_to_write(size);
            std::vector<uint32_t> zeros(size);
            std::vector<uint32_t> readback_vec = {};
            for (int i = 0; i < size; i++) {
                vector_to_write[i] = i;
                zeros[i] = 0;
            }

            eth_broadcast.broadcast_write_to_cluster(
                vector_to_write.data(),
                vector_to_write.size() * sizeof(uint32_t),
                address,
                {},
                rows_to_exclude,
                cols_to_exclude,
                true);
            cluster.wait_for_non_mmio_flush(chip_id);

            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                const CoreCoord translated_core =
                    cluster.get_soc_descriptor(chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
                if (rows_to_exclude.find(translated_core.y) != rows_to_exclude.end()) {
                    continue;
                }
                if (cols_to_exclude.find(translated_core.x) != cols_to_exclude.end()) {
                    continue;
                }
                test_utils::read_data_from_device(
                    cluster, readback_vec, chip_id, core, address, vector_to_write.size() * sizeof(uint32_t));
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from chip " << chip_id << " core " << core.str()
                    << " does not match what was broadcasted for size " << size;
                cluster.write_to_device(
                    zeros.data(),
                    zeros.size() * sizeof(std::uint32_t),
                    chip_id,
                    core,
                    address);  // Clear any written data
                readback_vec = {};
            }
        }
        cluster.wait_for_non_mmio_flush(chip_id);
    }
    cluster.close_device();
}

TEST(SiliconDriverWH, LargeAddressTlb) {
    Cluster cluster;

    const CoreCoord ARC_CORE = cluster.get_soc_descriptor(0).get_cores(CoreType::ARC).at(0);

    set_barrier_params(cluster);

    // Address of the reset unit in ARC core:
    uint64_t arc_reset_noc = 0x880030000ULL;

    // Offset to the scratch registers in the reset unit:
    uint64_t scratch_offset = 0x60;

    // Map a TLB to the reset unit in ARC core:
    cluster.configure_tlb(0, ARC_CORE, tt::umd::wormhole::STATIC_TLB_SIZE, arc_reset_noc);

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
