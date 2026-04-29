// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "blackhole/eth_l1_address_map.h"
#include "blackhole/l1_address_map.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/semver.hpp"

using namespace tt::umd;

constexpr std::uint32_t DRAM_BARRIER_BASE = 0;

static void set_barrier_params(Cluster& cluster) {
    // Populate address map and NOC parameters that the driver needs for memory barriers and remote transactions.
    cluster.set_barrier_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, DRAM_BARRIER_BASE});
}

TEST(SiliconDriverBH, CreateDestroy) {
    DeviceParams default_params;
    for (int i = 0; i < 50; i++) {
        Cluster cluster;
        set_barrier_params(cluster);
        test_utils::safe_test_cluster_start(&cluster);
        cluster.close_device();
    }
}

TEST(SiliconDriverBH, UnalignedStaticTLB_RW) {
    Cluster cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    set_barrier_params(cluster);

    // Do this only for a single chip to speed up the test.
    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);
    for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
        // Statically mapping a 2MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
        cluster.configure_tlb(
            chip_id, core, tt::umd::blackhole::STATIC_TLB_SIZE, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
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

TEST(SiliconDriverBH, StaticTLB_RW) {
    Cluster cluster;
    set_barrier_params(cluster);

    // Do this only for a single chip to speed up the test.
    auto chip_id = *cluster.get_target_mmio_device_ids().begin();
    auto& sdesc = cluster.get_soc_descriptor(chip_id);
    for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
        // Statically mapping a 2MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
        cluster.configure_tlb(
            chip_id, core, tt::umd::blackhole::STATIC_TLB_SIZE, l1_mem::address_map::NCRISC_FIRMWARE_BASE);
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

TEST(SiliconDriverBH, DynamicTLB_RW) {
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
    printf("Target Tensix cores completed\n");

    // Target DRAM channel 0.
    std::vector<uint32_t> dram_vector_to_write = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    address = 0x400;
    int NUM_CHANNELS = sdesc.get_num_dram_channels();
    for (int loop = 0; loop < 100; loop++) {
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            std::vector<CoreCoord> chan = sdesc.get_dram_cores().at(ch);
            CoreCoord subchan = chan.at(0);
            cluster.write_to_device(
                vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, subchan, address);
            cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited
            test_utils::read_data_from_device(cluster, readback_vec, chip_id, subchan, address, 40);
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from core " << subchan.x << "-" << subchan.y << "does not match what was written";
            cluster.wait_for_non_mmio_flush();
            cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, subchan, address);
            cluster.wait_for_non_mmio_flush();
            readback_vec = {};
            address += 0x20;  // Increment by uint32_t size for each write
        }
    }
    printf("Target DRAM completed\n");

    cluster.close_device();
}

// TODO(#2485): Re-enable. Writes and reads are not synchronized so they can land on the device out of order; broke
// after PR #2455.
TEST(SiliconDriverBH, DISABLED_MultiThreadedDevice) {
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

TEST(SiliconDriverBH, MultiThreadedMemBar) {
    // Have 2 threads read and write from a single device concurrently
    // All (fairly large) transactions go through a static TLB.
    // We want to make sure the memory barrier is thread/process safe.

    // Memory barrier flags get sent to address 0 for all channels in this test.
    uint32_t base_addr = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    Cluster cluster;
    set_barrier_params(cluster);
    for (auto chip_id : cluster.get_target_device_ids()) {
        // Iterate over devices and only setup static TLBs for functional worker cores.
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 2MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE.
            cluster.configure_tlb(chip_id, core, tt::umd::blackhole::STATIC_TLB_SIZE, base_addr);
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

// Verifies that all ETH channels are classified as either active/idle.
TEST(ClusterBH, TotalNumberOfEthCores) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    const uint32_t num_eth_cores = cluster->get_soc_descriptor(0).get_cores(CoreType::ETH).size();

    ClusterDescriptor* cluster_desc = cluster->get_cluster_description();
    const uint32_t num_active_channels = cluster_desc->get_active_eth_channels(0).size();
    const uint32_t num_idle_channels = cluster_desc->get_idle_eth_channels(0).size();

    EXPECT_EQ(num_eth_cores, num_active_channels + num_idle_channels);
}

TEST(ClusterBH, PCIECores) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    for (ChipId chip : cluster->get_target_device_ids()) {
        const auto& pcie_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::PCIE);

        EXPECT_EQ(pcie_cores.size(), 1);

        const auto& harvested_pcie_cores = cluster->get_soc_descriptor(chip).get_harvested_cores(CoreType::PCIE);

        EXPECT_EQ(harvested_pcie_cores.size(), 1);

        EXPECT_NE(pcie_cores.at(0).x, harvested_pcie_cores.at(0).x);
    }
}

TEST(ClusterBH, L2CPUCores) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    for (ChipId chip : cluster->get_target_device_ids()) {
        const auto& l2cpu_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::L2CPU);
        const auto& harvested_l2cpu_cores = cluster->get_soc_descriptor(chip).get_harvested_cores(CoreType::L2CPU);

        EXPECT_LE(harvested_l2cpu_cores.size(), 2);
        EXPECT_EQ(l2cpu_cores.size() + harvested_l2cpu_cores.size(), 4);
    }
}
