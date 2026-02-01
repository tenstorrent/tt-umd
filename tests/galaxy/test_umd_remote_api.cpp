// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <numeric>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "test_galaxy_common.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/wormhole/test_wh_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "wormhole/eth_interface.h"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

using namespace tt::umd;

void run_remote_read_write_test(uint32_t vector_size, bool dram_write) {
    Cluster device;

    test::utils::set_barrier_params(device);

    test_utils::safe_test_cluster_start(&device);

    // Test.
    std::vector<uint32_t> vector_to_write(vector_size);
    std::iota(vector_to_write.begin(), vector_to_write.end(), 0);
    float write_size = vector_to_write.size() * 4;
    std::vector<uint32_t> readback_vec = {};

    std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;

    for (const auto& chip : device.get_target_device_ids()) {
        std::vector<float> write_bw;
        std::vector<float> read_bw;
        for (int loop = 0; loop < 10; loop++) {
            std::vector<CoreCoord> target_cores;
            if (dram_write) {
                target_cores = device.get_soc_descriptor(chip).get_cores(CoreType::DRAM);
            } else {
                target_cores = device.get_soc_descriptor(chip).get_cores(CoreType::TENSIX);
            }
            for (const CoreCoord& core : target_cores) {
                auto start = std::chrono::high_resolution_clock::now();
                device.write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip, core, address);
                device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = double(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
                write_bw.push_back(float((write_size / (1024 * 1024 * 1024)) / (duration / 1e6)));
                // std::cout << "  chip " << chip << " core " << target_core.str() << " " << duration << std::endl;

                start = std::chrono::high_resolution_clock::now();
                test_utils::read_data_from_device(device, readback_vec, chip, core, address, write_size);
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
    Cluster device;
    auto target_devices = device.get_target_device_ids();

    // Verify that sender chip and receiver chip are in the cluster.
    auto it = target_devices.find(sender_core.chip);
    ASSERT_TRUE(it != target_devices.end())
        << "Sender core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";

    it = target_devices.find(receiver_core.chip);
    ASSERT_TRUE(it != target_devices.end())
        << "Receiver core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";

    test::utils::set_barrier_params(device);

    test_utils::safe_test_cluster_start(&device);

    // Test.
    std::vector<uint32_t> vector_to_write(vector_size);
    std::iota(vector_to_write.begin(), vector_to_write.end(), 0);
    float write_size = vector_to_write.size() * 4;
    std::vector<uint32_t> readback_vec = {};

    std::vector<float> send_bw;
    // Set up data in sender core.
    device.write_to_device(
        vector_to_write.data(),
        vector_to_write.size() * sizeof(std::uint32_t),
        sender_core.chip,
        sender_core.core,
        sender_core.addr);
    device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

    // Send data from sender core to receiver core.
    auto start = std::chrono::high_resolution_clock::now();
    move_data(device, sender_core, receiver_core, write_size);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = double(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    send_bw.push_back(float((write_size / (1024 * 1024 * 1024)) / (duration / 1e6)));
    // std::cout << "move data duration "<< duration << std::endl;

    // Verify data is correct in receiver core.
    test_utils::read_data_from_device(
        device, readback_vec, receiver_core.chip, receiver_core.core, receiver_core.addr, write_size);
    EXPECT_EQ(vector_to_write, readback_vec)
        << "Vector read back from core " << receiver_core.str() << " does not match what was written";

    // TODO: add flag to enable the latency dumps to a perf json
    // std::cout << " Sending " << write_size << " bytes from " <<
    // sender_core.str() << " to " << receiver_core.str() << " @ " <<
    // std::reduce(send_bw.begin(), send_bw.end()) / send_bw.size() << " GB/s" <<
    // std::endl;

    device.close_device();
}

// L1 to L1.
TEST(GalaxyDataMovement, TwoChipMoveData1) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(4, CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x5000);
    tt_multichip_core_addr receiver_core(5, CoreCoord(25, 27, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000);
    run_data_mover_test(100, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(31, CoreCoord(19, 19, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x5000);
    receiver_core = tt_multichip_core_addr(9, CoreCoord(24, 24, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000);
    run_data_mover_test(30000, sender_core, receiver_core);
}

// L1 to Dram.
TEST(GalaxyDataMovement, TwoChipMoveData2) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(1, CoreCoord(19, 20, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x30000);
    tt_multichip_core_addr receiver_core(6, CoreCoord(5, 0, CoreType::DRAM, CoordSystem::TRANSLATED), 0x0);
    run_data_mover_test(2000, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(11, CoreCoord(20, 20, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x50000);
    receiver_core = tt_multichip_core_addr(5, CoreCoord(0, 0, CoreType::DRAM, CoordSystem::TRANSLATED), 0x60000);
    run_data_mover_test(20000, sender_core, receiver_core);
}

// Dram to L1.
TEST(GalaxyDataMovement, TwoChipMoveData3) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(8, CoreCoord(5, 9, CoreType::DRAM, CoordSystem::TRANSLATED), 0x90000);
    tt_multichip_core_addr receiver_core(21, CoreCoord(18, 25, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x5200);
    run_data_mover_test(1200, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(11, CoreCoord(5, 5, CoreType::DRAM, CoordSystem::TRANSLATED), 0x40000);
    receiver_core = tt_multichip_core_addr(18, CoreCoord(24, 23, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x7000);
    run_data_mover_test(8800, sender_core, receiver_core);
}

// Dram to Dram.
TEST(GalaxyDataMovement, TwoChipMoveData4) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(7, CoreCoord(0, 6, CoreType::DRAM, CoordSystem::TRANSLATED), 0x300000);
    tt_multichip_core_addr receiver_core(19, CoreCoord(0, 0, CoreType::DRAM, CoordSystem::TRANSLATED), 0x300000);
    run_data_mover_test(1200, sender_core, receiver_core);

    sender_core = tt_multichip_core_addr(15, CoreCoord(5, 2, CoreType::DRAM, CoordSystem::TRANSLATED), 0x400000);
    receiver_core = tt_multichip_core_addr(16, CoreCoord(0, 11, CoreType::DRAM, CoordSystem::TRANSLATED), 0x400000);
    run_data_mover_test(8800, sender_core, receiver_core);
}

void run_data_broadcast_test(
    uint32_t vector_size,
    tt_multichip_core_addr sender_core,
    const std::vector<tt_multichip_core_addr>& receiver_cores) {
    Cluster device;
    auto target_devices = device.get_target_device_ids();

    // Verify that sender chip and receiver chip are in the cluster.
    auto it = target_devices.find(sender_core.chip);
    ASSERT_TRUE(it != target_devices.end())
        << "Sender core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";

    for (const auto& receiver_core : receiver_cores) {
        it = target_devices.find(receiver_core.chip);
        ASSERT_TRUE(it != target_devices.end())
            << "Receiver core is on chip " << sender_core.chip << " which is not in the Galaxy cluster";
    }

    test::utils::set_barrier_params(device);

    test_utils::safe_test_cluster_start(&device);

    // Test.
    std::vector<uint32_t> vector_to_write(vector_size);
    std::iota(vector_to_write.begin(), vector_to_write.end(), 0);
    float write_size = vector_to_write.size() * 4;
    std::vector<uint32_t> readback_vec = {};

    std::vector<float> send_bw;
    //  Set up data in sender core.
    device.write_to_device(
        vector_to_write.data(),
        vector_to_write.size() * sizeof(std::uint32_t),
        sender_core.chip,
        sender_core.core,
        sender_core.addr);
    device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

    // Send data from sender core to receiver core.
    auto start = std::chrono::high_resolution_clock::now();
    broadcast_data(device, sender_core, receiver_cores, write_size);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = double(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    send_bw.push_back(float((write_size / (1024 * 1024 * 1024)) / (duration / 1e6)));
    // std::cout << "broadcast duration "<< duration << std::endl;

    // Verify data is correct in receiver core.
    for (const auto& receiver_core : receiver_cores) {
        test_utils::read_data_from_device(
            device, readback_vec, receiver_core.chip, receiver_core.core, receiver_core.addr, write_size);
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

// L1 to L1 single chip.
TEST(GalaxyDataMovement, BroadcastData1) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(4, CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x5000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    for (const CoreCoord& core : sdesc.get_cores(CoreType::TENSIX)) {
        receiver_cores.push_back(tt_multichip_core_addr(5, core, 0x6000));
    }
    run_data_broadcast_test(100, sender_core, receiver_cores);
}

// L1 to L1 multi chip.
TEST(GalaxyDataMovement, BroadcastData2) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(12, CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x5000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(
        tt_multichip_core_addr(1, CoreCoord(19, 19, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(2, CoreCoord(19, 20, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(3, CoreCoord(19, 21, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(4, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(5, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(6, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(7, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(8, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(9, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(10, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(11, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(12, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(13, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(14, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(15, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(16, CoreCoord(19, 22, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    run_data_broadcast_test(1000, sender_core, receiver_cores);
}

// Dram to L1.
TEST(GalaxyDataMovement, BroadcastData3) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(10, CoreCoord(0, 0, CoreType::DRAM, CoordSystem::TRANSLATED), 0x20000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(
        tt_multichip_core_addr(5, CoreCoord(18, 24, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x5000));
    receiver_cores.push_back(
        tt_multichip_core_addr(10, CoreCoord(18, 25, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(15, CoreCoord(18, 26, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x7000));
    receiver_cores.push_back(
        tt_multichip_core_addr(20, CoreCoord(18, 27, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x8000));
    run_data_broadcast_test(2000, sender_core, receiver_cores);
}

// L1 to Dram.
TEST(GalaxyDataMovement, BroadcastData4) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(17, CoreCoord(24, 24, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x20000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(
        tt_multichip_core_addr(21, CoreCoord(0, 1, CoreType::DRAM, CoordSystem::TRANSLATED), 0x5000));
    receiver_cores.push_back(
        tt_multichip_core_addr(22, CoreCoord(0, 6, CoreType::DRAM, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(23, CoreCoord(5, 1, CoreType::DRAM, CoordSystem::TRANSLATED), 0x7000));
    receiver_cores.push_back(
        tt_multichip_core_addr(24, CoreCoord(5, 9, CoreType::DRAM, CoordSystem::TRANSLATED), 0x8000));
    receiver_cores.push_back(
        tt_multichip_core_addr(25, CoreCoord(5, 4, CoreType::DRAM, CoordSystem::TRANSLATED), 0x9000));
    receiver_cores.push_back(
        tt_multichip_core_addr(26, CoreCoord(5, 6, CoreType::DRAM, CoordSystem::TRANSLATED), 0x10000));
    run_data_broadcast_test(150, sender_core, receiver_cores);
}

// Dram to Dram.
TEST(GalaxyDataMovement, BroadcastData5) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});

    tt_multichip_core_addr sender_core(31, CoreCoord(19, 19, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x20000);
    std::vector<tt_multichip_core_addr> receiver_cores;

    receiver_cores.push_back(
        tt_multichip_core_addr(21, CoreCoord(0, 1, CoreType::DRAM, CoordSystem::TRANSLATED), 0x5000));
    receiver_cores.push_back(
        tt_multichip_core_addr(30, CoreCoord(0, 6, CoreType::DRAM, CoordSystem::TRANSLATED), 0x6000));
    receiver_cores.push_back(
        tt_multichip_core_addr(11, CoreCoord(5, 1, CoreType::DRAM, CoordSystem::TRANSLATED), 0x7000));
    receiver_cores.push_back(
        tt_multichip_core_addr(17, CoreCoord(5, 9, CoreType::DRAM, CoordSystem::TRANSLATED), 0x8000));
    run_data_broadcast_test(2500, sender_core, receiver_cores);
}

// L1 to L1 cores on many chips
// TODO: Failing with mismatch.
TEST(GalaxyDataMovement, DISABLED_BroadcastData6) {
    SocDescriptor sdesc(tt::ARCH::WORMHOLE_B0, {.noc_translation_enabled = true});
    tt_multichip_core_addr sender_core(1, CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x5000);
    std::vector<tt_multichip_core_addr> receiver_cores;
    for (int i = 2; i < 33; ++i) {
        receiver_cores.push_back(
            tt_multichip_core_addr(i, CoreCoord(19, 19, CoreType::TENSIX, CoordSystem::TRANSLATED), 0x7000));
    }
    run_data_broadcast_test(10000, sender_core, receiver_cores);
}
