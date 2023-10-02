// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "gtest/gtest.h"
#include <tt_device.h>
#include "device_data.hpp"
#include "l1_address_map.h"
#include "eth_interface.h"
#include "host_mem_address_map.h"
#include <thread>
#include <util.hpp>

void set_params_for_remote_txn(tt_SiliconDevice& device) {
    // Populate address map and NOC parameters that the driver needs for remote transactions
    device.set_driver_host_address_params({host_mem::address_map::ETH_ROUTING_BLOCK_SIZE, host_mem::address_map::ETH_ROUTING_BUFFERS_START});

    device.set_driver_eth_interface_params({NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS, ETH_RACK_COORD_WIDTH, CMD_BUF_SIZE_MASK, MAX_BLOCK_SIZE,
                                            REQUEST_CMD_QUEUE_BASE, RESPONSE_CMD_QUEUE_BASE, CMD_COUNTERS_SIZE_BYTES, REMOTE_UPDATE_PTR_SIZE_BYTES,
                                            CMD_DATA_BLOCK, CMD_WR_REQ, CMD_WR_ACK, CMD_RD_REQ, CMD_RD_DATA, CMD_BUF_SIZE, CMD_DATA_BLOCK_DRAM, ETH_ROUTING_DATA_BUFFER_ADDR,
                                            REQUEST_ROUTING_CMD_QUEUE_BASE, RESPONSE_ROUTING_CMD_QUEUE_BASE, CMD_BUF_PTR_MASK});
    
    device.set_device_l1_address_params({l1_mem::address_map::NCRISC_FIRMWARE_BASE, l1_mem::address_map::FIRMWARE_BASE,
                                  l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE,
                                  l1_mem::address_map::TRISC_BASE});
}

TEST(SiliconDriverWH, Harvesting) {
    setenv("TT_BACKEND_HARVESTED_ROWS", "30,60", 1);
    std::set<chip_id_t> target_devices = {0, 1};
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {}; // Don't set any dynamic TLBs in this test
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    tt_SiliconDevice device = tt_SiliconDevice("./tests/soc_descs/wormhole_b0_8x10.yaml", GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
    device.clean_system_resources();
    auto sdesc_per_chip = device.get_virtual_soc_descriptors();

    ASSERT_EQ(device.using_harvested_soc_descriptors(), true) << "Expected Driver to have performed harvesting";

    for(const auto& chip : sdesc_per_chip) {
        ASSERT_EQ(chip.second.workers.size(), 48) << "Expected SOC descriptor with harvesting to have 48 workers for chip" << chip.first;
    }
    ASSERT_EQ(device.get_harvesting_masks_for_soc_descriptors().at(0), 30) << "Expected first chip to have harvesting mask of 30";
    ASSERT_EQ(device.get_harvesting_masks_for_soc_descriptors().at(1), 60) << "Expected second chip to have harvesting mask of 60";
    unsetenv("TT_BACKEND_HARVESTED_ROWS");
}

TEST(SiliconDriverWH, CustomSocDesc) {
    setenv("TT_BACKEND_HARVESTED_ROWS", "30,60", 1);
    std::set<chip_id_t> target_devices = {0, 1};
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {}; // Don't set any dynamic TLBs in this test
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    // Initialize the driver with a 1x1 descriptor and explictly do not perform harvesting
    tt_SiliconDevice device = tt_SiliconDevice("./tests/soc_descs/wormhole_b0_1x1.yaml", GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config, false, false);
    device.clean_system_resources();
    auto sdesc_per_chip = device.get_virtual_soc_descriptors();
    
    ASSERT_EQ(device.using_harvested_soc_descriptors(), false) << "SOC descriptors should not be modified when harvesting is disabled";
    for(const auto& chip : sdesc_per_chip) {
        ASSERT_EQ(chip.second.workers.size(), 1) << "Expected 1x1 SOC descriptor to be unmodified by driver";
    }
    unsetenv("TT_BACKEND_HARVESTED_ROWS");
}

TEST(SiliconDriverWH, HarvestingRuntime) {
    setenv("TT_BACKEND_HARVESTED_ROWS", "30,60", 1);

    auto get_static_tlb_index = [] (tt_xy_pair target) {
        if (target.x >= 5) {
            target.x -= 1;
        }
        target.x -= 1;

        if (target.y >= 6) {
            target.y -= 1;
        }
        target.y -= 1;

        return target.y * 8 + target.x;
    };

    std::set<chip_id_t> target_devices = {0, 1};

    uint32_t num_host_mem_ch_per_mmio_device = 1;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {{"SMALL_READ_WRITE_TLB", 157}}; // Use both static and dynamic TLBs here
    
    tt_SiliconDevice device = tt_SiliconDevice("./tests/soc_descs/wormhole_b0_8x10.yaml", GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
    set_params_for_remote_txn(device);
    auto mmio_devices = device.get_target_mmio_device_ids();
    
    for(int i = 0; i < target_devices.size(); i++) {
        // Iterate over MMIO devices and only setup static TLBs for worker cores
        if(std::find(mmio_devices.begin(), mmio_devices.end(), i) != mmio_devices.end()) {
            auto& sdesc = device.get_virtual_soc_descriptors().at(i);
            for(auto& core : sdesc.workers) {
                // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.  
                device.configure_tlb(i, core, get_static_tlb_index(core), l1_mem::address_map::NCRISC_FIRMWARE_BASE);
            }
        } 
    }
    device.setup_core_to_tlb_map(get_static_tlb_index);
    
    tt_device_params default_params;
    device.start_device(default_params);
    device.clean_system_resources();

    for(int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> dynamic_tlb_vector_to_write = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    std::vector<uint32_t> dynamic_readback_vec = {};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        std::uint32_t dynamic_write_address = 0x40000000;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core : device.get_virtual_soc_descriptors().at(i).workers) {
                device.write_to_device(vector_to_write, tt_cxy_pair(i, core), address, "");
                device.write_to_device(vector_to_write, tt_cxy_pair(i, core), dynamic_write_address, "SMALL_READ_WRITE_TLB");
                device.wait_for_non_mmio_flush(); // Barrier to ensure that all writes over ethernet were commited
                device.read_from_device(readback_vec, tt_cxy_pair(i, core), address, 40, "");
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.wait_for_non_mmio_flush();
                device.write_to_device(zeros, tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB"); // Clear any written data
                device.read_from_device(dynamic_readback_vec, tt_cxy_pair(i, core), dynamic_write_address, 40, "SMALL_READ_WRITE_TLB");
                readback_vec = {};
                dynamic_readback_vec = {};
            }
            address += 0x20; // Increment by uint32_t size for each write
            dynamic_write_address += 0x20;
        }
    }
    device.close_device(); 
    unsetenv("TT_BACKEND_HARVESTED_ROWS");  
}

TEST(SiliconDriverWH, StaticTLB_RW) {
    auto get_static_tlb_index = [] (tt_xy_pair target) {
        if (target.x >= 5) {
            target.x -= 1;
        }
        target.x -= 1;

        if (target.y >= 6) {
            target.y -= 1;
        }
        target.y -= 1;

        return target.y * 8 + target.x;
    };

    std::set<chip_id_t> target_devices = {0, 1};

    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {}; // Don't set any dynamic TLBs in this test
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    
    tt_SiliconDevice device = tt_SiliconDevice("./tests/soc_descs/wormhole_b0_8x10.yaml", GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
    set_params_for_remote_txn(device);
    auto mmio_devices = device.get_target_mmio_device_ids();

    for(int i = 0; i < target_devices.size(); i++) {
        // Iterate over MMIO devices and only setup static TLBs for worker cores
        if(std::find(mmio_devices.begin(), mmio_devices.end(), i) != mmio_devices.end()) {
            auto& sdesc = device.get_virtual_soc_descriptors().at(i);
            for(auto& core : sdesc.workers) {
                // Statically mapping a 1MB TLB to this core, starting from address NCRISC_FIRMWARE_BASE.  
                device.configure_tlb(i, core, get_static_tlb_index(core), l1_mem::address_map::NCRISC_FIRMWARE_BASE);
            }
        } 
    }

    device.setup_core_to_tlb_map(get_static_tlb_index);
    
    tt_device_params default_params;
    device.start_device(default_params);
    device.clean_system_resources();

    for(int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    // Check functionality of Static TLBs by reading adn writing from statically mapped address space
    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core : device.get_virtual_soc_descriptors().at(i).workers) {
                device.write_to_device(vector_to_write, tt_cxy_pair(i, core), address, "");
                device.wait_for_non_mmio_flush(); // Barrier to ensure that all writes over ethernet were commited
                device.read_from_device(readback_vec, tt_cxy_pair(i, core), address, 40, "");
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.wait_for_non_mmio_flush();
                device.write_to_device(zeros, tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB"); // Clear any written data
                readback_vec = {};
            }
            address += 0x20; // Increment by uint32_t size for each write
        }
    }
    device.close_device();    
}

TEST(SiliconDriverWH, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs to be reconfigured for each transaction
    std::set<chip_id_t> target_devices = {0, 1};

    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    dynamic_tlb_config.insert({"SMALL_READ_WRITE_TLB", 157}); // Use this for all reads and writes to worker cores
    tt_SiliconDevice device = tt_SiliconDevice("./tests/soc_descs/wormhole_b0_8x10.yaml",  GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);

    set_params_for_remote_txn(device);

    tt_device_params default_params;
    device.start_device(default_params);
    device.clean_system_resources();

    for(int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = {};

    for(int i = 0; i < target_devices.size(); i++) {
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for(int loop = 0; loop < 100; loop++){ // Write to each core a 100 times at different statically mapped addresses
            for(auto& core : device.get_virtual_soc_descriptors().at(i).workers) {
                device.write_to_device(vector_to_write, tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB");
                device.wait_for_non_mmio_flush(); // Barrier to ensure that all writes over ethernet were commited
                device.read_from_device(readback_vec, tt_cxy_pair(i, core), address, 40, "SMALL_READ_WRITE_TLB");
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                device.wait_for_non_mmio_flush();
                device.write_to_device(zeros, tt_cxy_pair(i, core), address, "SMALL_READ_WRITE_TLB");
                readback_vec = {};
            }
            address += 0x20; // Increment by uint32_t size for each write
        }
    }
    device.close_device();
}

TEST(SiliconDriverWH, MultiThreadedDevice) {
    // Have 2 threads read and write from a single device concurrently
    // All transactions go through a single Dynamic TLB. We want to make sure this is thread/process safe

    std::set<chip_id_t> target_devices = {0};


    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    dynamic_tlb_config.insert({"SMALL_READ_WRITE_TLB", 157}); // Use this for all reads and writes to worker cores
    tt_SiliconDevice device = tt_SiliconDevice("./tests/soc_descs/wormhole_b0_8x10.yaml", GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
    
    set_params_for_remote_txn(device);

    tt_device_params default_params;
    device.start_device(default_params);
    device.clean_system_resources();

    for(int i = 0; i < target_devices.size(); i++) {
        device.deassert_risc_reset(i);
    }

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
        for(int loop = 0; loop < 100; loop++) {
            for(auto& core : device.get_virtual_soc_descriptors().at(0).workers) {
                device.write_to_device(vector_to_write, tt_cxy_pair(0, core), address, "SMALL_READ_WRITE_TLB");
                device.read_from_device(readback_vec, tt_cxy_pair(0, core), address, 40, "SMALL_READ_WRITE_TLB");
                ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                readback_vec = {};
            }
            address += 0x20;
        }
    });

    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x30000000;
        for(auto& core_ls : device.get_virtual_soc_descriptors().at(0).dram_cores) {
            for(int loop = 0; loop < 100; loop++) {
                for(auto& core : core_ls) {
                    device.write_to_device(vector_to_write, tt_cxy_pair(0, core), address, "SMALL_READ_WRITE_TLB");
                    device.read_from_device(readback_vec, tt_cxy_pair(0, core), address, 40, "SMALL_READ_WRITE_TLB");
                    ASSERT_EQ(vector_to_write, readback_vec) << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";
                    readback_vec = {};
                }
                address += 0x20;
            }
        }
    });

    th1.join();
    th2.join();
    device.close_device();
}