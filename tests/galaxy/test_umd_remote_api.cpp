// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <numeric>
#include <filesystem>

#include "gtest/gtest.h"
#include "common/logger.hpp"
#include "tt_cluster_descriptor.h"
#include "tt_device.h"
#include "eth_interface.h"
#include "host_mem_address_map.h"
#include "l1_address_map.h"

#include "test_galaxy_common.h"
#include "tests/wormhole/test_wh_common.h"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "tests/test_utils/device_test_utils.hpp"

static const std::string SOC_DESC_PATH = "tests/soc_descs/wormhole_b0_8x10.yaml";

void run_remote_read_write_test(uint32_t vector_size, bool dram_write) {
    // Galaxy Setup
    std::string cluster_desc_path = test_utils::GetClusterDescYAML();
    std::shared_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(cluster_desc_path);
    std::set<chip_id_t> target_devices = {};
    for (const auto& chip : cluster_desc->get_all_chips()) {
        target_devices.insert(chip);
    }

    uint32_t num_host_mem_ch_per_mmio_device = 1;

    tt_SiliconDevice device = tt_SiliconDevice(
        test_utils::GetAbsPath(SOC_DESC_PATH), cluster_desc_path, target_devices, num_host_mem_ch_per_mmio_device, false, true);
    const auto sdesc_per_chip = device.get_virtual_soc_descriptors();

    tt::umd::test::utils::set_params_for_remote_txn(device);

    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();

    // Test
    std::vector<uint32_t> vector_to_write(vector_size);
    std::iota(vector_to_write.begin(), vector_to_write.end(), 0);
    float write_size = vector_to_write.size() * 4;
    std::vector<uint32_t> readback_vec = {};

    std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;

    for (const auto& chip : target_devices) {
        std::vector<float> write_bw;
        std::vector<float> read_bw;
        for (int loop = 0; loop < 10; loop++) {
            std::vector<tt_xy_pair> target_cores;
            if (dram_write) {
                for (const auto& subchan_cores : sdesc_per_chip.at(chip).dram_cores) {
                    target_cores.insert(target_cores.end(), subchan_cores.begin(), subchan_cores.end());
                }
            } else {
                target_cores = sdesc_per_chip.at(chip).workers;
            }
            for (const auto& core : target_cores) {
                tt_cxy_pair target_core = tt_cxy_pair(chip, core);
                auto start = std::chrono::high_resolution_clock::now();
                device.write_to_device(vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), target_core, address, "SMALL_READ_WRITE_TLB");
                device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = double(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                write_bw.push_back(float((write_size / (1024 * 1024 * 1024)) / (duration / 1e6)));
                // std::cout << "  chip " << chip << " core " << target_core.str() << " " << duration << std::endl;

                start = std::chrono::high_resolution_clock::now();
                test_utils::read_data_from_device(device, readback_vec, target_core, address, write_size, "SMALL_READ_WRITE_TLB");
                end = std::chrono::high_resolution_clock::now();
                duration = double(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                // std::cout << " read chip " << chip << " core " << target_core.str()<< " " << duration << std::endl;
                read_bw.push_back(float((write_size / (1024 * 1024 * 1024)) / (duration / 1e6)));
                EXPECT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << "does not match what was written";
                readback_vec = {};
            }
            if (write_size <= 256) {
                address += 20;
            } else {
                address += 32;  // block mode write addresses must be 32 byte aligned
            }
        }
        // TODO: add flag to enable the latency dumps to a perf json
        //  std::cout << " Pushing " << write_size << " bytes to chip " << chip << " @ " <<
        //  std::reduce(write_bw.begin(), write_bw.end()) / write_bw.size()
        //  << " GB/s" << std::endl; std::cout << " Reading " << write_size << " bytes from chip " << chip << " @ " <<
        //  std::reduce(read_bw.begin(), read_bw.end()) / read_bw.size() << " GB/s" << std::endl;
    }

    device.close_device();
}

// write and read back 10 uint32_t to L1 of every worker core on every chip in the cluster
TEST(GalaxyBasicReadWrite, SmallRemoteL1ReadWrite) { run_remote_read_write_test(10, false); }

// write and read back 10 uint32_t to every dram core on every chip in the cluster
TEST(GalaxyBasicReadWrite, SmallRemoteDramReadWrite) { run_remote_read_write_test(10, true); }

// write and read back 256 uint32_t to L1 of every worker core on every chip in the cluster
TEST(GalaxyBasicReadWrite, LargeRemoteL1ReadWrite) { run_remote_read_write_test(256, false); }

// write and read back 256 uint32_t to every dram core on every chip in the cluster
TEST(GalaxyBasicReadWrite, LargeRemoteDramReadWrite) { run_remote_read_write_test(256, true); }

// block write and read back 256 uint32_t to L1 of every worker core on every chip in the cluster
TEST(GalaxyBasicReadWrite, SmallRemoteL1BlockReadWrite) { run_remote_read_write_test(345, false); }

// write and read back 345 uint32_t to every dram core on every chip in the cluster
TEST(GalaxyBasicReadWrite, SmallRemoteDramBlockReadWrite) { run_remote_read_write_test(345, true); }

// block write and read back 2048 uint32_t to L1 of every worker core on every chip in the cluster
TEST(GalaxyBasicReadWrite, LargeRemoteL1BlockReadWrite) { run_remote_read_write_test(2048, false); }

// block write and read back 2048 uint32_t to every dram core on every chip in the cluster
TEST(GalaxyBasicReadWrite, LargeRemoteDramBlockReadWrite) { run_remote_read_write_test(2048, true); }

// block write and read back 2048 uint32_t to L1 of every worker core on every chip in the cluster

void run_data_mover_test(
    uint32_t vector_size, tt_multichip_core_addr sender_core, tt_multichip_core_addr receiver_core) {
    // Galaxy Setup
    std::string cluster_desc_path = test_utils::GetClusterDescYAML();
    std::shared_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(cluster_desc_path);
    std::set<chip_id_t> target_devices = {};
    for (const auto& chip : cluster_desc->get_all_chips()) {
        target_devices.insert(chip);
    }

    // Verify that sender chip and receiver chip are in the cluster
    auto it = std::find(target_devices.begin(), target_devices.end(), sender_core.chip);
    ASSERT_TRUE(it != target_devices.end())
        << "Sender core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";

    it = std::find(target_devices.begin(), target_devices.end(), receiver_core.chip);
    ASSERT_TRUE(it != target_devices.end())
        << "Receiver core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";

    uint32_t num_host_mem_ch_per_mmio_device = 1;

    tt_SiliconDevice device = tt_SiliconDevice(
        test_utils::GetAbsPath(SOC_DESC_PATH), cluster_desc_path, target_devices, num_host_mem_ch_per_mmio_device, false, true);

    tt::umd::test::utils::set_params_for_remote_txn(device);

    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();

    // Test
    std::vector<uint32_t> vector_to_write(vector_size);
    std::iota(vector_to_write.begin(), vector_to_write.end(), 0);
    float write_size = vector_to_write.size() * 4;
    std::vector<uint32_t> readback_vec = {};

    std::vector<float> send_bw;
    // Set up data in sender core
    device.write_to_device(
        vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(sender_core.chip, sender_core.core), sender_core.addr, "SMALL_READ_WRITE_TLB");
    device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

    // Send data from sender core to receiver core
    auto start = std::chrono::high_resolution_clock::now();
    move_data(device, sender_core, receiver_core, write_size);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = double(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    send_bw.push_back(float((write_size / (1024 * 1024 * 1024)) / (duration / 1e6)));
    // std::cout << "move data duration "<< duration << std::endl;

    // Verify data is correct in receiver core
    test_utils::read_data_from_device(
        device,
        readback_vec,
        tt_cxy_pair(receiver_core.chip, receiver_core.core),
        receiver_core.addr,
        write_size,
        "SMALL_READ_WRITE_TLB");
    EXPECT_EQ(vector_to_write, readback_vec)
        << "Vector read back from core " << receiver_core.str() << " does not match what was written";

    // TODO: add flag to enable the latency dumps to a perf json
    // std::cout << " Sending " << write_size << " bytes from " <<
    // sender_core.str() << " to " << receiver_core.str() << " @ " <<
    // std::reduce(send_bw.begin(), send_bw.end()) / send_bw.size() << " GB/s" <<
    // std::endl;

    device.close_device();
}

// L1 to L1
TEST(GalaxyDataMovement, TwoChipMoveData1) {
    tt_multichip_core_addr sender_core(4, tt_xy_pair(1, 1), 0x5000);
    tt_multichip_core_addr receiver_core(5, tt_xy_pair(9, 11), 0x6000);
    run_data_mover_test(100, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(31, tt_xy_pair(2, 2), 0x5000);
    receiver_core = tt_multichip_core_addr(9, tt_xy_pair(8, 8), 0x6000);
    run_data_mover_test(30000, sender_core, receiver_core);
}

// L1 to Dram
TEST(GalaxyDataMovement, TwoChipMoveData2) {
    tt_multichip_core_addr sender_core(1, tt_xy_pair(2, 3), 0x30000);
    tt_multichip_core_addr receiver_core(6, tt_xy_pair(5, 0), 0x0);
    run_data_mover_test(2000, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(11, tt_xy_pair(3, 3), 0x50000);
    receiver_core = tt_multichip_core_addr(5, tt_xy_pair(0, 0), 0x60000);
    run_data_mover_test(20000, sender_core, receiver_core);
}

// Dram to L1
TEST(GalaxyDataMovement, TwoChipMoveData3) {
    tt_multichip_core_addr sender_core(8, tt_xy_pair(5, 9), 0x90000);
    tt_multichip_core_addr receiver_core(21, tt_xy_pair(1, 9), 0x5200);
    run_data_mover_test(1200, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(11, tt_xy_pair(5, 5), 0x40000);
    receiver_core = tt_multichip_core_addr(18, tt_xy_pair(8, 7), 0x7000);
    run_data_mover_test(8800, sender_core, receiver_core);
}

// Dram to Dram
TEST(GalaxyDataMovement, TwoChipMoveData4) {
    tt_multichip_core_addr sender_core(7, tt_xy_pair(0, 6), 0x300000);
    tt_multichip_core_addr receiver_core(19, tt_xy_pair(0, 0), 0x300000);
    run_data_mover_test(1200, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(15, tt_xy_pair(5, 2), 0x400000);
    receiver_core = tt_multichip_core_addr(16, tt_xy_pair(0, 11), 0x400000);
    run_data_mover_test(8800, sender_core, receiver_core);
}

void run_data_broadcast_test(
    uint32_t vector_size, tt_multichip_core_addr sender_core, std::vector<tt_multichip_core_addr> receiver_cores) {
    // Galaxy Setup
    std::string cluster_desc_path = test_utils::GetClusterDescYAML();
    std::shared_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(cluster_desc_path);
    std::set<chip_id_t> target_devices = {};
    for (const auto& chip : cluster_desc->get_all_chips()) {
        target_devices.insert(chip);
    }

    // Verify that sender chip and receiver chip are in the cluster
    auto it = std::find(target_devices.begin(), target_devices.end(), sender_core.chip);
    ASSERT_TRUE(it != target_devices.end())
        << "Sender core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";

    for (const auto& receiver_core : receiver_cores) {
        it = std::find(target_devices.begin(), target_devices.end(), receiver_core.chip);
        ASSERT_TRUE(it != target_devices.end())
            << "Receiver core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";
    }

    uint32_t num_host_mem_ch_per_mmio_device = 1;

    tt_SiliconDevice device = tt_SiliconDevice(
        test_utils::GetAbsPath(SOC_DESC_PATH), cluster_desc_path, target_devices, num_host_mem_ch_per_mmio_device, false, true);

    tt::umd::test::utils::set_params_for_remote_txn(device);

    tt_device_params default_params;
    device.start_device(default_params);
    device.deassert_risc_reset();

    // Test
    std::vector<uint32_t> vector_to_write(vector_size);
    std::iota(vector_to_write.begin(), vector_to_write.end(), 0);
    float write_size = vector_to_write.size() * 4;
    std::vector<uint32_t> readback_vec = {};

    std::vector<float> send_bw;
    //  Set up data in sender core
    device.write_to_device(
        vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), tt_cxy_pair(sender_core.chip, sender_core.core), sender_core.addr, "SMALL_READ_WRITE_TLB");
    device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

    // Send data from sender core to receiver core
    auto start = std::chrono::high_resolution_clock::now();
    broadcast_data(device, sender_core, receiver_cores, write_size);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = double(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    send_bw.push_back(float((write_size / (1024 * 1024 * 1024)) / (duration / 1e6)));
    // std::cout << "broadcast duration "<< duration << std::endl;

    // Verify data is correct in receiver core
    for (const auto& receiver_core : receiver_cores) {
        test_utils::read_data_from_device(
            device,
            readback_vec,
            tt_cxy_pair(receiver_core.chip, receiver_core.core),
            receiver_core.addr,
            write_size,
            "SMALL_READ_WRITE_TLB");
        EXPECT_EQ(vector_to_write, readback_vec)
            << "Vector read back from core " << receiver_core.str() << " does not match what was written";
        readback_vec = {};
    }

    // TODO: add flag to enable the latency dumps to a perf json
    // std::cout << " Broadcasting " << write_size << " bytes from " <<
    // sender_core.str() << " to " << receiver_cores.size() << " cores @ " <<
    // std::reduce(send_bw.begin(), send_bw.end()) / send_bw.size() << " GB/s" <<
    // std::endl;

    device.close_device();
}

// L1 to L1 single chip
TEST(GalaxyDataMovement, BroadcastData1) {
    tt_SocDescriptor sdesc(test_utils::GetAbsPath(SOC_DESC_PATH));

    tt_multichip_core_addr sender_core(4, tt_xy_pair(1, 1), 0x5000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    for (const auto& core : sdesc.workers) {
        receiver_cores.push_back(tt_multichip_core_addr(5, core, 0x6000));
    }
    run_data_broadcast_test(100, sender_core, receiver_cores);
}

// L1 to L1 multi chip
TEST(GalaxyDataMovement, BroadcastData2) {
    tt_SocDescriptor sdesc(test_utils::GetAbsPath(SOC_DESC_PATH));

    tt_multichip_core_addr sender_core(12, tt_xy_pair(1, 1), 0x5000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(tt_multichip_core_addr(1, tt_xy_pair(2, 2), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(2, tt_xy_pair(2, 3), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(3, tt_xy_pair(2, 4), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(4, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(5, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(6, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(7, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(8, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(9, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(10, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(11, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(12, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(13, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(14, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(15, tt_xy_pair(2, 5), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(16, tt_xy_pair(2, 5), 0x6000));
    run_data_broadcast_test(1000, sender_core, receiver_cores);
}

// Dram to L1
TEST(GalaxyDataMovement, BroadcastData3) {
    tt_SocDescriptor sdesc(test_utils::GetAbsPath(SOC_DESC_PATH));

    tt_multichip_core_addr sender_core(10, tt_xy_pair(0, 0), 0x20000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(tt_multichip_core_addr(5, tt_xy_pair(1, 8), 0x5000));
    receiver_cores.push_back(tt_multichip_core_addr(10, tt_xy_pair(1, 9), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(15, tt_xy_pair(1, 10), 0x7000));
    receiver_cores.push_back(tt_multichip_core_addr(20, tt_xy_pair(1, 11), 0x8000));
    run_data_broadcast_test(2000, sender_core, receiver_cores);
}

// L1 to Dram
TEST(GalaxyDataMovement, BroadcastData4) {
    tt_SocDescriptor sdesc(test_utils::GetAbsPath(SOC_DESC_PATH));

    tt_multichip_core_addr sender_core(17, tt_xy_pair(8, 8), 0x20000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(tt_multichip_core_addr(21, tt_xy_pair(0, 1), 0x5000));
    receiver_cores.push_back(tt_multichip_core_addr(22, tt_xy_pair(0, 6), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(23, tt_xy_pair(5, 1), 0x7000));
    receiver_cores.push_back(tt_multichip_core_addr(24, tt_xy_pair(5, 9), 0x8000));
    receiver_cores.push_back(tt_multichip_core_addr(25, tt_xy_pair(5, 4), 0x9000));
    receiver_cores.push_back(tt_multichip_core_addr(26, tt_xy_pair(5, 6), 0x10000));
    run_data_broadcast_test(150, sender_core, receiver_cores);
}

// Dram to Dram
TEST(GalaxyDataMovement, BroadcastData5) {
    tt_SocDescriptor sdesc(test_utils::GetAbsPath(SOC_DESC_PATH));

    tt_multichip_core_addr sender_core(31, tt_xy_pair(2, 2), 0x20000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(tt_multichip_core_addr(21, tt_xy_pair(0, 1), 0x5000));
    receiver_cores.push_back(tt_multichip_core_addr(30, tt_xy_pair(0, 6), 0x6000));
    receiver_cores.push_back(tt_multichip_core_addr(11, tt_xy_pair(5, 1), 0x7000));
    receiver_cores.push_back(tt_multichip_core_addr(17, tt_xy_pair(5, 9), 0x8000));
    run_data_broadcast_test(2500, sender_core, receiver_cores);
}

// L1 to L1 cores on many chips
// TODO: Failing with mismatch
TEST(GalaxyDataMovement, DISABLED_BroadcastData6) {
    tt_multichip_core_addr sender_core(1, tt_xy_pair(1, 1), 0x5000);
    std::vector<tt_multichip_core_addr> receiver_cores;
    for (int i = 2; i < 33; ++i) {
        receiver_cores.push_back(tt_multichip_core_addr(i, tt_xy_pair(2, 2), 0x7000));
    }
    run_data_broadcast_test(10000, sender_core, receiver_cores);
}
