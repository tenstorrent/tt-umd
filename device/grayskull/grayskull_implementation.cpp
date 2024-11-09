// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "grayskull_implementation.h"

#include "src/firmware/riscv/grayskull/host_mem_address_map.h"
#include "src/firmware/riscv/grayskull/eth_interface.h"

#include "device/tt_device.h"

constexpr std::uint32_t NOC_ADDR_LOCAL_BITS = 32; // source: noc_parameters.h, unique for GS
constexpr std::uint32_t NOC_ADDR_NODE_ID_BITS = 6; // source: noc_parameters.h, common for GS && WH && BH

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
            .offset = grayskull::TLB_16M_OFFSET,
        };
    } else if (tlb_index >= grayskull::TLB_BASE_INDEX_2M) {
        return tlb_configuration{
            .size = grayskull::DYNAMIC_TLB_2M_SIZE,
            .base = grayskull::DYNAMIC_TLB_2M_BASE,
            .cfg_addr = grayskull::DYNAMIC_TLB_2M_CFG_ADDR,
            .index_offset = tlb_index - grayskull::TLB_BASE_INDEX_2M,
            .offset = grayskull::TLB_2M_OFFSET,
        };
    } else {
        return tlb_configuration{
            .size = grayskull::DYNAMIC_TLB_1M_SIZE,
            .base = grayskull::DYNAMIC_TLB_1M_BASE,
            .cfg_addr = grayskull::DYNAMIC_TLB_1M_CFG_ADDR,
            .index_offset = tlb_index - grayskull::TLB_BASE_INDEX_1M,
            .offset = grayskull::TLB_1M_OFFSET,
        };
    }
}

std::optional<std::tuple<std::uint64_t, std::uint64_t>> grayskull_implementation::describe_tlb(
    std::int32_t tlb_index) const {
    std::uint32_t TLB_COUNT_1M = 156;
    std::uint32_t TLB_COUNT_2M = 10;
    std::uint32_t TLB_COUNT_16M = 20;

    std::uint32_t TLB_BASE_1M = 0;
    std::uint32_t TLB_BASE_2M = TLB_COUNT_1M * (1 << 20);
    std::uint32_t TLB_BASE_16M = TLB_BASE_2M + TLB_COUNT_2M * (1 << 21);

    if (tlb_index < 0) {
        return std::nullopt;
    }

    if (tlb_index >= 0 && tlb_index < TLB_COUNT_1M) {
        std::uint32_t size = 1 << 20;
        return std::tuple(TLB_BASE_1M + size * tlb_index, size);
    } else if (tlb_index >= 0 && tlb_index < TLB_COUNT_1M + TLB_COUNT_2M) {
        auto tlb_offset = tlb_index - TLB_COUNT_1M;
        auto size = 1 << 21;
        return std::tuple(TLB_BASE_2M + tlb_offset * size, size);
    } else if (tlb_index >= 0 and tlb_index < TLB_COUNT_1M + TLB_COUNT_2M + TLB_COUNT_16M) {
        auto tlb_offset = tlb_index - (TLB_COUNT_1M + TLB_COUNT_2M);
        auto size = 1 << 24;
        return std::tuple(TLB_BASE_16M + tlb_offset * size, size);
    }

    return std::nullopt;
}

std::pair<std::uint64_t, std::uint64_t> grayskull_implementation::get_tlb_data(
    std::uint32_t tlb_index, const tlb_data &data) const {
    if (tlb_index < grayskull::TLB_COUNT_1M) {
        return data.apply_offset(grayskull::TLB_1M_OFFSET);
    } else if (tlb_index < grayskull::TLB_COUNT_1M + grayskull::TLB_COUNT_2M) {
        return data.apply_offset(grayskull::TLB_2M_OFFSET);
    } else if (tlb_index < grayskull::TLB_COUNT_1M + grayskull::TLB_COUNT_2M + grayskull::TLB_COUNT_16M) {
        return data.apply_offset(grayskull::TLB_16M_OFFSET);
    } else {
        throw std::runtime_error("Invalid TLB index for Grayskull arch");
    }
}

tt_driver_host_address_params grayskull_implementation::get_host_address_params() const {
    return {::grayskull::host_mem::address_map::ETH_ROUTING_BLOCK_SIZE, ::grayskull::host_mem::address_map::ETH_ROUTING_BUFFERS_START};
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
