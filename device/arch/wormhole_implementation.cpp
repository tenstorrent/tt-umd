// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arch/wormhole_implementation.hpp"

#include <cstdint>
#include <stdexcept>
#include <tuple>

#include "assert.hpp"
#include "umd/device/cluster.hpp"
#include "wormhole/eth_interface.h"
#include "wormhole/eth_l1_address_map.h"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

namespace tt::umd {

constexpr std::uint32_t NOC_ADDR_LOCAL_BITS = 36;   // source: noc_parameters.h, common for WH && BH
constexpr std::uint32_t NOC_ADDR_NODE_ID_BITS = 6;  // source: noc_parameters.h, common for WH && BH

std::tuple<xy_pair, xy_pair> wormhole_implementation::multicast_workaround(xy_pair start, xy_pair end) const {
    // When multicasting there is a rare case where including the multicasting node in the box can result in a backup
    // and the multicasted data not reaching all endpoints specified. As a workaround we exclude the pci endpoint from
    // the multicast. This doesn't cause any problems with making some tensix cores inaccessible because column 0 (which
    // we are excluding) doesn't have tensix.
    start.x = start.x == 0 ? 1 : start.x;
    return std::make_tuple(start, end);
}

tlb_configuration wormhole_implementation::get_tlb_configuration(uint32_t tlb_index) const {
    if (tlb_index >= wormhole::TLB_BASE_INDEX_16M) {
        return tlb_configuration{
            .size = wormhole::DYNAMIC_TLB_16M_SIZE,
            .base = wormhole::DYNAMIC_TLB_16M_BASE,
            .cfg_addr = wormhole::DYNAMIC_TLB_16M_CFG_ADDR,
            .index_offset = tlb_index - wormhole::TLB_BASE_INDEX_16M,
            .tlb_offset = wormhole::DYNAMIC_TLB_16M_BASE +
                          (tlb_index - wormhole::TLB_BASE_INDEX_16M) * wormhole::DYNAMIC_TLB_16M_SIZE,
            .offset = wormhole::TLB_16M_OFFSET,
        };
    } else if (tlb_index >= wormhole::TLB_BASE_INDEX_2M) {
        return tlb_configuration{
            .size = wormhole::DYNAMIC_TLB_2M_SIZE,
            .base = wormhole::DYNAMIC_TLB_2M_BASE,
            .cfg_addr = wormhole::DYNAMIC_TLB_2M_CFG_ADDR,
            .index_offset = tlb_index - wormhole::TLB_BASE_INDEX_2M,
            .tlb_offset = wormhole::DYNAMIC_TLB_2M_BASE +
                          (tlb_index - wormhole::TLB_BASE_INDEX_2M) * wormhole::DYNAMIC_TLB_2M_SIZE,
            .offset = wormhole::TLB_2M_OFFSET,
        };
    } else {
        return tlb_configuration{
            .size = wormhole::DYNAMIC_TLB_1M_SIZE,
            .base = wormhole::DYNAMIC_TLB_1M_BASE,
            .cfg_addr = wormhole::DYNAMIC_TLB_1M_CFG_ADDR,
            .index_offset = tlb_index - wormhole::TLB_BASE_INDEX_1M,
            .tlb_offset = wormhole::DYNAMIC_TLB_1M_BASE +
                          (tlb_index - wormhole::TLB_BASE_INDEX_1M) * wormhole::DYNAMIC_TLB_1M_SIZE,
            .offset = wormhole::TLB_1M_OFFSET,
        };
    }
}

DeviceL1AddressParams wormhole_implementation::get_l1_address_params() const {
    // L1 barrier base and erisc barrier base should be explicitly set by the client.
    // Setting some default values here, but it should be ultimately overridden by the client.
    return {
        ::l1_mem::address_map::L1_BARRIER_BASE,
        ::eth_l1_mem::address_map::ERISC_BARRIER_BASE,
        ::eth_l1_mem::address_map::FW_VERSION_ADDR};
}

DriverHostAddressParams wormhole_implementation::get_host_address_params() const {
    return {
        ::wormhole::host_mem::address_map::ETH_ROUTING_BLOCK_SIZE,
        ::wormhole::host_mem::address_map::ETH_ROUTING_BUFFERS_START};
}

DriverEthInterfaceParams wormhole_implementation::get_eth_interface_params() const {
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

DriverNocParams wormhole_implementation::get_noc_params() const { return {NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS}; }

// TODO: integrate noc_port for DRAM core type inside the function.
uint64_t wormhole_implementation::get_noc_reg_base(
    const CoreType core_type, const uint32_t noc, const uint32_t  /*noc_port*/) const {
    if (noc == 0) {
        for (const auto& noc_pair : wormhole::NOC0_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
    } else {
        for (const auto& noc_pair : wormhole::NOC1_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
    }

    throw std::runtime_error("Invalid core type or NOC for getting NOC register addr base.");
}

uint32_t wormhole_implementation::get_soft_reset_reg_value(RiscType risc_type) const {
    if ((risc_type & RiscType::ALL_NEO) != RiscType::NONE) {
        // Throw if any of the NEO cores are selected.
        TT_THROW("NEO risc cores should not be used on Wormhole architecture.");
    }

    // Fill up Tensix related bits based on architecture agnostic bits.
    if ((risc_type & RiscType::ALL) != RiscType::NONE) {
        risc_type |= RiscType::ALL_TENSIX;
    }
    if ((risc_type & RiscType::ALL_TRISCS) != RiscType::NONE) {
        risc_type |= RiscType::ALL_TENSIX_TRISCS;
    }
    if ((risc_type & RiscType::ALL_DATA_MOVEMENT) != RiscType::NONE) {
        risc_type |= RiscType::ALL_TENSIX_DMS;
    }

    uint32_t soft_reset_reg_value = 0;
    if ((risc_type & RiscType::BRISC) != RiscType::NONE) {
        soft_reset_reg_value |= wormhole::SOFT_RESET_BRISC;
    }
    if ((risc_type & RiscType::TRISC0) != RiscType::NONE) {
        soft_reset_reg_value |= wormhole::SOFT_RESET_TRISC0;
    }
    if ((risc_type & RiscType::TRISC1) != RiscType::NONE) {
        soft_reset_reg_value |= wormhole::SOFT_RESET_TRISC1;
    }
    if ((risc_type & RiscType::TRISC2) != RiscType::NONE) {
        soft_reset_reg_value |= wormhole::SOFT_RESET_TRISC2;
    }
    if ((risc_type & RiscType::NCRISC) != RiscType::NONE) {
        soft_reset_reg_value |= wormhole::SOFT_RESET_NCRISC;
    }

    return soft_reset_reg_value;
}

RiscType wormhole_implementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    RiscType risc_type = RiscType::NONE;
    if (soft_reset_reg_value & wormhole::SOFT_RESET_BRISC) {
        risc_type |= RiscType::BRISC;
    }
    if (soft_reset_reg_value & wormhole::SOFT_RESET_TRISC0) {
        risc_type |= RiscType::TRISC0;
    }
    if (soft_reset_reg_value & wormhole::SOFT_RESET_TRISC1) {
        risc_type |= RiscType::TRISC1;
    }
    if (soft_reset_reg_value & wormhole::SOFT_RESET_TRISC2) {
        risc_type |= RiscType::TRISC2;
    }
    if (soft_reset_reg_value & wormhole::SOFT_RESET_NCRISC) {
        risc_type |= RiscType::NCRISC;
    }

    // Set arhitecture agnostic bits based on tensix bits.
    if ((risc_type & RiscType::ALL_TENSIX) != RiscType::NONE) {
        risc_type |= RiscType::ALL;
    }
    if ((risc_type & RiscType::ALL_TENSIX_TRISCS) != RiscType::NONE) {
        risc_type |= RiscType::ALL_TRISCS;
    }
    if ((risc_type & RiscType::ALL_TENSIX_DMS) != RiscType::NONE) {
        risc_type |= RiscType::ALL_DATA_MOVEMENT;
    }

    return risc_type;
}
}  // namespace tt::umd
