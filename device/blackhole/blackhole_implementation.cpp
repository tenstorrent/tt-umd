// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/blackhole_implementation.h"

#include "blackhole/eth_interface.h"
#include "blackhole/eth_l1_address_map.h"
#include "blackhole/host_mem_address_map.h"
#include "blackhole/l1_address_map.h"
#include "logger.hpp"
#include "umd/device/cluster.h"

constexpr std::uint32_t NOC_ADDR_LOCAL_BITS = 36;   // source: noc_parameters.h, common for WH && BH
constexpr std::uint32_t NOC_ADDR_NODE_ID_BITS = 6;  // source: noc_parameters.h, common for WH && BH

namespace tt::umd {

std::tuple<xy_pair, xy_pair> blackhole_implementation::multicast_workaround(xy_pair start, xy_pair end) const {
    // TODO: This is copied from wormhole_implementation. It should be implemented properly.

    // When multicasting there is a rare case where including the multicasting node in the box can result in a backup
    // and the multicasted data not reaching all endpoints specified. As a workaround we exclude the pci endpoint from
    // the multicast. This doesn't cause any problems with making some tensix cores inaccessible because column 0 (which
    // we are excluding) doesn't have tensix.
    start.x = start.x == 0 ? 1 : start.x;
    return std::make_tuple(start, end);
}

tlb_configuration blackhole_implementation::get_tlb_configuration(uint32_t tlb_index) const {
    // If TLB index is in range for 4GB tlbs (8 TLBs after 202 TLBs for 2MB)
    if (tlb_index >= blackhole::TLB_COUNT_2M && tlb_index < blackhole::TLB_COUNT_2M + blackhole::TLB_COUNT_4G) {
        return tlb_configuration{
            .size = blackhole::DYNAMIC_TLB_4G_SIZE,
            .base = blackhole::DYNAMIC_TLB_4G_BASE,
            .cfg_addr = blackhole::DYNAMIC_TLB_4G_CFG_ADDR,
            .index_offset = tlb_index - blackhole::TLB_BASE_INDEX_4G,
            .tlb_offset = blackhole::DYNAMIC_TLB_4G_BASE +
                          (tlb_index - blackhole::TLB_BASE_INDEX_4G) * blackhole::DYNAMIC_TLB_4G_SIZE,
            .offset = blackhole::TLB_4G_OFFSET,
        };
    }

    return tlb_configuration{
        .size = blackhole::DYNAMIC_TLB_2M_SIZE,
        .base = blackhole::DYNAMIC_TLB_2M_BASE,
        .cfg_addr = blackhole::DYNAMIC_TLB_2M_CFG_ADDR,
        .index_offset = tlb_index - blackhole::TLB_BASE_INDEX_2M,
        .tlb_offset = blackhole::DYNAMIC_TLB_2M_BASE +
                      (tlb_index - blackhole::TLB_BASE_INDEX_2M) * blackhole::DYNAMIC_TLB_2M_SIZE,
        .offset = blackhole::TLB_2M_OFFSET,
    };
}

std::pair<std::uint64_t, std::uint64_t> blackhole_implementation::get_tlb_data(
    std::uint32_t tlb_index, const tlb_data& data) const {
    if (tlb_index < blackhole::TLB_COUNT_2M) {
        return data.apply_offset(blackhole::TLB_2M_OFFSET);
    } else {
        throw std::runtime_error("Invalid TLB index for Blackhole arch");
    }
}

tt_device_l1_address_params blackhole_implementation::get_l1_address_params() const {
    // L1 barrier base and erisc barrier base should be explicitly set by the client.
    // Setting some default values here, but it should be ultimately overridden by the client.
    return {
        ::l1_mem::address_map::L1_BARRIER_BASE,
        ::eth_l1_mem::address_map::ERISC_BARRIER_BASE,
        ::eth_l1_mem::address_map::FW_VERSION_ADDR};
}

tt_driver_host_address_params blackhole_implementation::get_host_address_params() const {
    return {
        ::blackhole::host_mem::address_map::ETH_ROUTING_BLOCK_SIZE,
        ::blackhole::host_mem::address_map::ETH_ROUTING_BUFFERS_START};
}

tt_driver_eth_interface_params blackhole_implementation::get_eth_interface_params() const {
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

tt_driver_noc_params blackhole_implementation::get_noc_params() const {
    return {NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS};
}

namespace blackhole {
std::vector<tt_xy_pair> get_pcie_cores(const BoardType board_type, const bool is_chip_remote) {
    // Default to type 1 chip.
    if (board_type == BoardType::UNKNOWN) {
        return PCIE_CORES_TYPE1;
    }
    auto chip_type = get_blackhole_chip_type(board_type, is_chip_remote);
    return chip_type == BlackholeChipType::Type1 ? PCIE_CORES_TYPE1 : PCIE_CORES_TYPE2;
}
}  // namespace blackhole

}  // namespace tt::umd
