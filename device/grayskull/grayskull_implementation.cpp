// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/grayskull_implementation.h"

#include "grayskull/eth_interface.h"
#include "grayskull/host_mem_address_map.h"
#include "grayskull/l1_address_map.h"
#include "umd/device/cluster.h"

constexpr std::uint32_t NOC_ADDR_LOCAL_BITS = 32;   // source: noc_parameters.h, unique for GS
constexpr std::uint32_t NOC_ADDR_NODE_ID_BITS = 6;  // source: noc_parameters.h, common for GS && WH && BH

namespace tt::umd {

std::tuple<xy_pair, xy_pair> grayskull_implementation::multicast_workaround(xy_pair start, xy_pair end) const {
    return std::make_tuple(start, end);
}

tlb_configuration grayskull_implementation::get_tlb_configuration(uint32_t tlb_index) const {
    if (tlb_index >= grayskull::TLB_BASE_INDEX_16M) {
        return tlb_configuration{
            .size = grayskull::DYNAMIC_TLB_16M_SIZE,
            .base = grayskull::DYNAMIC_TLB_16M_BASE,
            .cfg_addr = grayskull::DYNAMIC_TLB_16M_CFG_ADDR,
            .index_offset = tlb_index - grayskull::TLB_BASE_INDEX_16M,
            .tlb_offset = grayskull::DYNAMIC_TLB_16M_BASE +
                          (tlb_index - grayskull::TLB_BASE_INDEX_16M) * grayskull::DYNAMIC_TLB_16M_SIZE,
            .offset = grayskull::TLB_16M_OFFSET,
        };
    } else if (tlb_index >= grayskull::TLB_BASE_INDEX_2M) {
        return tlb_configuration{
            .size = grayskull::DYNAMIC_TLB_2M_SIZE,
            .base = grayskull::DYNAMIC_TLB_2M_BASE,
            .cfg_addr = grayskull::DYNAMIC_TLB_2M_CFG_ADDR,
            .index_offset = tlb_index - grayskull::TLB_BASE_INDEX_2M,
            .tlb_offset = grayskull::DYNAMIC_TLB_2M_BASE +
                          (tlb_index - grayskull::TLB_BASE_INDEX_2M) * grayskull::DYNAMIC_TLB_2M_SIZE,
            .offset = grayskull::TLB_2M_OFFSET,
        };
    } else {
        return tlb_configuration{
            .size = grayskull::DYNAMIC_TLB_1M_SIZE,
            .base = grayskull::DYNAMIC_TLB_1M_BASE,
            .cfg_addr = grayskull::DYNAMIC_TLB_1M_CFG_ADDR,
            .index_offset = tlb_index - grayskull::TLB_BASE_INDEX_1M,
            .tlb_offset = grayskull::DYNAMIC_TLB_1M_BASE +
                          (tlb_index - grayskull::TLB_BASE_INDEX_1M) * grayskull::DYNAMIC_TLB_1M_SIZE,
            .offset = grayskull::TLB_1M_OFFSET,
        };
    }
}

tt_device_l1_address_params grayskull_implementation::get_l1_address_params() const {
    // L1 barrier base should be explicitly set by the client.
    // Setting some default value here, but it should be ultimately overridden by the client.
    // Grayskull doesn't have ethernet cores, so no eth params are set here.
    return {::l1_mem::address_map::L1_BARRIER_BASE, 0, 0};
}

tt_driver_host_address_params grayskull_implementation::get_host_address_params() const {
    return {
        ::grayskull::host_mem::address_map::ETH_ROUTING_BLOCK_SIZE,
        ::grayskull::host_mem::address_map::ETH_ROUTING_BUFFERS_START};
}

tt_driver_eth_interface_params grayskull_implementation::get_eth_interface_params() const {
    return {
        ETH_RACK_COORD_WIDTH,
        CMD_BUF_SIZE_MASK,
        MAX_BLOCK_SIZE,
        REQUEST_CMD_QUEUE_BASE,
        RESPONSE_CMD_QUEUE_BASE,
        CMD_COUNTERS_SIZE_BYTES,
        REMOTE_UPDATE_PTR_SIZE_BYTES,
        CMD_DATA_BLOCK,
        CMD_WR_REQ,
        CMD_WR_ACK,
        CMD_RD_REQ,
        CMD_RD_DATA,
        CMD_BUF_SIZE,
        CMD_DATA_BLOCK_DRAM,
        ETH_ROUTING_DATA_BUFFER_ADDR,
        REQUEST_ROUTING_CMD_QUEUE_BASE,
        RESPONSE_ROUTING_CMD_QUEUE_BASE,
        CMD_BUF_PTR_MASK,
        CMD_ORDERED,
        CMD_BROADCAST,
    };
}

tt_driver_noc_params grayskull_implementation::get_noc_params() const {
    return {NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS};
}

}  // namespace tt::umd
