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
#include <memory>

#include "device/tt_cluster_descriptor.h"

void set_params_for_remote_txn(tt_SiliconDevice& device) {
    // Populate address map and NOC parameters that the driver needs for remote transactions
    device.set_driver_host_address_params({host_mem::address_map::ETH_ROUTING_BLOCK_SIZE, host_mem::address_map::ETH_ROUTING_BUFFERS_START});

    device.set_driver_eth_interface_params({NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS, ETH_RACK_COORD_WIDTH, CMD_BUF_SIZE_MASK, MAX_BLOCK_SIZE,
                                            REQUEST_CMD_QUEUE_BASE, RESPONSE_CMD_QUEUE_BASE, CMD_COUNTERS_SIZE_BYTES, REMOTE_UPDATE_PTR_SIZE_BYTES,
                                            CMD_DATA_BLOCK, CMD_WR_REQ, CMD_WR_ACK, CMD_RD_REQ, CMD_RD_DATA, CMD_BUF_SIZE, CMD_DATA_BLOCK_DRAM, ETH_ROUTING_DATA_BUFFER_ADDR,
                                            REQUEST_ROUTING_CMD_QUEUE_BASE, RESPONSE_ROUTING_CMD_QUEUE_BASE, CMD_BUF_PTR_MASK, CMD_BROADCAST});
    
    device.set_device_l1_address_params({l1_mem::address_map::NCRISC_FIRMWARE_BASE, l1_mem::address_map::FIRMWARE_BASE,
                                  l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE,
                                  l1_mem::address_map::TRISC_BASE});
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

    std::set<chip_id_t> target_devices = {0, 1}; // 2, 3, 4, 5, 6, 7, 8, 9, 10};

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

    std::cout << "deasserting tensix risc reset" << std::endl;
    // for(int i = 0; i < target_devices.size(); i++) {
    device.deassert_risc_reset();
    // }
    std::cout << "done" << std::endl;
    std::vector<uint32_t> vector_to_write(1000);
    for(int i = 0; i < vector_to_write.size(); i++) vector_to_write.at(i) = i + 15;
    std::vector<uint32_t> readback_vec = {};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uint32_t address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    std::cout << "Address: " <<  std::hex << address << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<uint32_t> rows_to_exclude = {0, 6};
    std::vector<uint32_t> cols_to_exclude = {0, 5};
    device.broadcast_write_to_cluster(vector_to_write.data(), vector_to_write.size() * 4, address, {}, rows_to_exclude, cols_to_exclude);
    // device.write_to_non_mmio_device(vector_to_write.data(), vector_to_write.size() * 4, tt_cxy_pair(1, 1, 1), address, false, racks_to_exclude_per_shelf, chips_to_exclude, rows_to_exclude, columns_to_exlude);
    float duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
    std::cout << "Write time: " << duration << std::endl;
    
    device.close_device();    
}

