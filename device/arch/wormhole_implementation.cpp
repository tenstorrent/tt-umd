// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arch/wormhole_implementation.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <string>
#include <tuple>

#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/wormhole_eth.hpp"
#include "umd/device/types/wormhole_l1.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace wormhole {
constexpr std::uint32_t NOC_ADDR_LOCAL_BITS = 36;   // source: noc_parameters.h, common for WH && BH
constexpr std::uint32_t NOC_ADDR_NODE_ID_BITS = 6;  // source: noc_parameters.h, common for WH && BH
}  // namespace wormhole

tlb_configuration WormholeImplementation::get_tlb_configuration(uint32_t tlb_index) const {
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

DeviceL1AddressParams WormholeImplementation::get_l1_address_params() const {
    // L1 barrier base and erisc barrier base should be explicitly set by the client.
    // Setting some default values here, but it should be ultimately overridden by the client.
    return {wormhole::L1_BARRIER_BASE, wormhole::ERISC_BARRIER_BASE, wormhole::ETH_FW_VERSION_ADDR};
}

DriverHostAddressParams WormholeImplementation::get_host_address_params() const {
    return {
        erisc_firmware::eth_routing::ETH_ROUTING_BLOCK_SIZE, erisc_firmware::eth_routing::ETH_ROUTING_BUFFERS_START};
}

DriverEthInterfaceParams WormholeImplementation::get_eth_interface_params() const {
    using namespace erisc_firmware::eth_routing;
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

DriverNocParams WormholeImplementation::get_noc_params() const {
    return {wormhole::NOC_ADDR_LOCAL_BITS, wormhole::NOC_ADDR_NODE_ID_BITS};
}

// TODO: integrate noc_port for DRAM core type inside the function.
uint64_t WormholeImplementation::get_noc_reg_base(
    const CoreType core_type, const uint32_t noc, const uint32_t noc_port) const {
    if (noc == 0) {
        for (const auto& noc_pair : wormhole::NOC0_CONTROL_REG_ADDR_BASE_MAP) {
            if (core_type == CoreType::DRAM) {
                return wormhole::DRAM_NOC0_CONTROL_REG_ADDR_BASE_MAP[noc_port];
            }
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    } else if (noc == 1) {
        for (const auto& noc_pair : wormhole::NOC1_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                if (core_type == CoreType::DRAM) {
                    return wormhole::DRAM_NOC1_CONTROL_REG_ADDR_BASE_MAP[noc_port];
                    ;
                }
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    }

    UMD_THROW(error::RuntimeError, fmt::format("Invalid NOC: {} for getting NOC register addr base.", noc));
}

uint32_t WormholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    if ((risc_type & RiscType::ALL_NEO) != RiscType::NONE) {
        // Throw if any of the NEO cores are selected.
        UMD_THROW(error::RuntimeError, "NEO risc cores should not be used on Wormhole architecture.");
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

RiscType WormholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
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
