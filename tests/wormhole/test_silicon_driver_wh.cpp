// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "gtest/gtest.h"
#include <tt_device.h>
#include "device_data.hpp"
#include "l1_address_map.h"
#include "eth_l1_address_map.h"
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
                                  l1_mem::address_map::TRISC_BASE, l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, eth_l1_mem::address_map::FW_VERSION_ADDR});
}


TEST(SiliconDriverWH, StaticTLB_RW) {
    std::set<chip_id_t> target_devices = {0, 1};

    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {}; // Don't set any dynamic TLBs in this test
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    
    tt_SiliconDevice device = tt_SiliconDevice("./tests/soc_descs/wormhole_b0_8x10.yaml", GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config);
    device.clean_system_resources();
    set_params_for_remote_txn(device);    
    tt_device_params default_params;
    device.start_device(default_params);
    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    auto start_time = std::chrono::high_resolution_clock::now();
    for(int i = 0 ; i < 100; i++) {
        device.write_to_sysmem(vector_to_write, 0, 0, 0);
    }
    std::cout << "Time taken for write: " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time).count() << std::endl;

    device.close_device();    
}

