// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include "blackhole/eth_l1_address_map.h"
#include "blackhole/host_mem_address_map.h"
#include "blackhole/l1_address_map.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/utils/semver.hpp"

using namespace tt::umd;

constexpr std::uint32_t DRAM_BARRIER_BASE = 0;

static void set_barrier_params(Cluster& cluster) {
    // Populate address map and NOC parameters that the driver needs for memory barriers and remote transactions.
    cluster.set_barrier_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, DRAM_BARRIER_BASE});
}

std::int32_t get_static_tlb_index(tt_xy_pair target) {
    bool is_eth_location =
        std::find(std::begin(tt::umd::blackhole::ETH_LOCATIONS), std::end(tt::umd::blackhole::ETH_LOCATIONS), target) !=
        std::end(tt::umd::blackhole::ETH_LOCATIONS);
    bool is_tensix_location =
        std::find(
            std::begin(tt::umd::blackhole::T6_X_LOCATIONS), std::end(tt::umd::blackhole::T6_X_LOCATIONS), target.x) !=
            std::end(tt::umd::blackhole::T6_X_LOCATIONS) &&
        std::find(
            std::begin(tt::umd::blackhole::T6_Y_LOCATIONS), std::end(tt::umd::blackhole::T6_Y_LOCATIONS), target.y) !=
            std::end(tt::umd::blackhole::T6_Y_LOCATIONS);
    if (is_eth_location) {
        if (target.y == 6) {
            target.y = 1;
        }

        if (target.x >= 5) {
            target.x -= 1;
        }
        target.x -= 1;

        int flat_index = target.y * 14 + target.x;
        int tlb_index = flat_index;
        return tlb_index;

    } else if (is_tensix_location) {
        if (target.x >= 8) {
            target.x -= 2;
        }
        target.x -= 1;  // First x index is 1

        target.y -= 2;  // First y index is 2

        int flat_index = target.y * 14 + target.x;

        // All 140 get single 2MB TLB.
        int tlb_index = tt::umd::blackhole::ETH_LOCATIONS.size() + flat_index;

        return tlb_index;
    } else {
        return -1;
    }
}

TEST(SiliconDriverBH, CreateDestroy) {
    DeviceParams default_params;
    for (int i = 0; i < 50; i++) {
        Cluster cluster;
        set_barrier_params(cluster);
        cluster.start_device(default_params);
        cluster.close_device();
    }
}

// TEST(SiliconDriverWH, CustomSocDesc) {
//     std::set<ChipId> target_devices = {0, 1};
//     std::unordered_map<ChipId, uint32_t> simulated_harvesting_masks = {{0, 30}, {1, 60}};
//     {
//         std::unique_ptr<ClusterDescriptor> cluster_desc_uniq =
//             Cluster::create_cluster_descriptor();
//         if (cluster_desc_uniq->get_number_of_chips() != target_devices.size()) {
//             GTEST_SKIP() << "SiliconDriverWH.Harvesting skipped because it can only be run on a two chip nebula
//             system";
//         }
//     }

//     uint32_t num_host_mem_ch_per_mmio_device = 1;
//     // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting
//     Cluster cluster = Cluster(
//         "./tests/soc_descs/wormhole_b0_1x1.yaml",
//         target_devices,
//         num_host_mem_ch_per_mmio_device,
//         false,
//         false,
//         simulated_harvesting_masks);

//     for (const auto& chip : sdesc_per_chip) {
//         ASSERT_EQ(chip.second.workers.size(), 1) << "Expected 1x1 SOC descriptor to be unmodified by driver";
//     }
// }

// TEST(SiliconDriverWH, HarvestingRuntime) {
//     auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

//     std::set<ChipId> target_devices = {0, 1};
//     std::unordered_map<ChipId, uint32_t> simulated_harvesting_masks = {{0, 30}, {1, 60}};
//     {
//         std::unique_ptr<ClusterDescriptor> cluster_desc_uniq =
//             Cluster::create_cluster_descriptor();
//         if (cluster_desc_uniq->get_number_of_chips() != target_devices.size()) {
//             GTEST_SKIP() << "SiliconDriverWH.Harvesting skipped because it can only be run on a two chip nebula
//             system";
//         }
//     }

//     uint32_t num_host_mem_ch_per_mmio_device = 1;

//     Cluster cluster = Cluster(
//         tt::ARCH::BLACKHOLE,
//         target_devices,
//         num_host_mem_ch_per_mmio_device,
//         false,
//         true,
//         simulated_harvesting_masks);
//     set_barrier_params(cluster);
//     auto mmio_devices = cluster.get_target_mmio_device_ids();

//     for (int i = 0; i < target_devices.size(); i++) {
//         // Iterate over MMIO devices and only setup static TLBs for worker cores
//         if (std::find(mmio_devices.begin(), mmio_devices.end(), i) != mmio_devices.end()) {
//             auto& sdesc = cluster.get_soc_descriptor(i);
//             for (auto& core : sdesc.workers) {
//                 // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
//                 cluster.configure_tlb(
//                     i, core, get_static_tlb_index_callback(core), l1_mem::address_map::NCRISC_FIRMWARE_BASE);
//             }
//         }
//     }

//     DeviceParams default_params;
//     cluster.start_device(default_params);

//     std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
//     std::vector<uint32_t> dynamic_readback_vec = {};
//     std::vector<uint32_t> readback_vec = {};
//     std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

//     for (int i = 0; i < target_devices.size(); i++) {
//         std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
//         std::uint32_t dynamic_write_address = 0x40000000;
//         for (int loop = 0; loop < 100;
//              loop++) {  // Write to each core a 100 times at different statically mapped addresses
//             for (auto& core : cluster.get_soc_descriptor(i).workers) {
//                 cluster.write_to_device(
//                     vector_to_write.data(),
//                     vector_to_write.size() * sizeof(std::uint32_t),
//                     tt_cxy_pair(i, core),
//                     address,
//                     "");
//                 cluster.write_to_device(
//                     vector_to_write.data(),
//                     vector_to_write.size() * sizeof(std::uint32_t),
//                     tt_cxy_pair(i, core),
//                     dynamic_write_address);
//                 cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

//                 test_utils::read_data_from_device(cluster, readback_vec, tt_cxy_pair(i, core), address, 40);
//                 test_utils::read_data_from_device(
//                     cluster,
//                     dynamic_readback_vec,
//                     tt_cxy_pair(i, core),
//                     dynamic_write_address,
//                     40);
//                 ASSERT_EQ(vector_to_write, readback_vec)
//                     << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
//                 ASSERT_EQ(vector_to_write, dynamic_readback_vec)
//                     << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
//                 cluster.wait_for_non_mmio_flush();

//                 cluster.write_to_device(
//                     zeros.data(),
//                     zeros.size() * sizeof(std::uint32_t),
//                     tt_cxy_pair(i, core),
//                     dynamic_write_address);  // Clear any written data
//                 cluster.write_to_device(
//                     zeros.data(),
//                     zeros.size() * sizeof(std::uint32_t),
//                     tt_cxy_pair(i, core),
//                     address,
//                     "");  // Clear any written data
//                 cluster.wait_for_non_mmio_flush();
//                 readback_vec = {};
//                 dynamic_readback_vec = {};
//             }
//             address += 0x20;  // Increment by uint32_t size for each write
//             dynamic_write_address += 0x20;
//         }
//     }
//     cluster.close_device();
// }

TEST(SiliconDriverBH, UnalignedStaticTLB_RW) {
    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores
    for (auto& chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (auto& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
            cluster.configure_tlb(
                chip_id,
                core,
                get_static_tlb_index_callback(sdesc.translate_coord_to(core, CoordSystem::TRANSLATED)),
                l1_mem::address_map::NCRISC_FIRMWARE_BASE);
        }
    }

    DeviceParams default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> unaligned_sizes = {3, 14, 21, 255, 362, 430, 1022, 1023, 1025};
    for (auto& chip_id : cluster.get_target_device_ids()) {
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

TEST(SiliconDriverBH, StaticTLB_RW) {
    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

    Cluster cluster;
    set_barrier_params(cluster);
    auto mmio_devices = cluster.get_target_mmio_device_ids();

    // Iterate over MMIO devices and only setup static TLBs for worker cores
    for (auto chip_id : mmio_devices) {
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 2MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.
            cluster.configure_tlb(
                chip_id,
                core,
                get_static_tlb_index_callback(sdesc.translate_coord_to(core, CoordSystem::TRANSLATED)),
                l1_mem::address_map::NCRISC_FIRMWARE_BASE);
        }
    }

    printf("MT: Static TLBs set\n");

    DeviceParams default_params;
    cluster.start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space
    for (auto chip_id : cluster.get_target_device_ids()) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for (int loop = 0; loop < 1;
             loop++) {  // Write to each core a 100 times at different statically mapped addresses
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
                cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited
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

TEST(SiliconDriverBH, DynamicTLB_RW) {
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
        for (int loop = 0; loop < 100;
             loop++) {  // Write to each core a 100 times at different statically mapped addresses
            for (const CoreCoord& core : cluster.get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)) {
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, core, address);
                cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited
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
    printf("Target Tensix cores completed\n");

    // Target DRAM channel 0
    std::vector<uint32_t> dram_vector_to_write = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    std::uint32_t address = 0x400;
    for (auto chip_id : cluster.get_target_device_ids()) {
        int NUM_CHANNELS = cluster.get_soc_descriptor(chip_id).get_num_dram_channels();
        for (int loop = 0; loop < 100;
             loop++) {  // Write to each core a 100 times at different statically mapped addresses
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                std::vector<CoreCoord> chan = cluster.get_soc_descriptor(chip_id).get_dram_cores().at(ch);
                CoreCoord subchan = chan.at(0);
                cluster.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip_id, subchan, address);
                cluster.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited
                test_utils::read_data_from_device(cluster, readback_vec, chip_id, subchan, address, 40);
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << subchan.x << "-"
                                                         << subchan.y << "does not match what was written";
                cluster.wait_for_non_mmio_flush();
                cluster.write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip_id, subchan, address);
                cluster.wait_for_non_mmio_flush();
                readback_vec = {};
                address += 0x20;  // Increment by uint32_t size for each write
            }
        }
    }
    printf("Target DRAM completed\n");

    cluster.close_device();
}

TEST(SiliconDriverBH, MultiThreadedDevice) {
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
        for (std::vector<CoreCoord> core_ls : cluster.get_soc_descriptor(0).get_dram_cores()) {
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

    // Memory barrier flags get sent to address 0 for all channels in this test
    auto get_static_tlb_index_callback = [](tt_xy_pair target) { return get_static_tlb_index(target); };

    uint32_t base_addr = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    Cluster cluster;
    set_barrier_params(cluster);
    for (auto chip_id : cluster.get_target_device_ids()) {
        // Iterate over devices and only setup static TLBs for functional worker cores
        auto& sdesc = cluster.get_soc_descriptor(chip_id);
        for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
            // Statically mapping a 1MB TLB to this core, starting from address DATA_BUFFER_SPACE_BASE.
            cluster.configure_tlb(
                chip_id,
                core,
                get_static_tlb_index_callback(sdesc.translate_coord_to(core, CoordSystem::TRANSLATED)),
                base_addr);
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

TEST(SiliconDriverBH, DISABLED_BroadcastWrite) {  // Cannot broadcast to tensix/ethernet and DRAM simultaneously on
                                                  // Blackhole .. wait_for_non_mmio_flush() is not working as expected?
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
        cluster.wait_for_non_mmio_flush();  // flush here so we don't simultaneously broadcast to DRAM?
        // Broadcast to DRAM
        cluster.broadcast_write_to_cluster(
            vector_to_write.data(),
            vector_to_write.size() * 4,
            address,
            {},
            rows_to_exclude_for_dram_broadcast,
            cols_to_exclude_for_dram_broadcast);
        cluster.wait_for_non_mmio_flush();

        for (const auto chip_id : cluster.get_target_device_ids()) {
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

TEST(SiliconDriverBH, DISABLED_VirtualCoordinateBroadcast) {  // same problem as above..
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

        for (const auto chip_id : cluster.get_target_device_ids()) {
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
