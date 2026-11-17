// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/wormhole_implementation.hpp"

#include "tt-umd/types/wormhole_eth.hpp"
#include "tt-umd/types/wormhole_l1.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace wormhole {

DriverHostAddressParams get_host_address_params() {
    UMD_THROW(error::RuntimeError, "The legacy Wormhole ETH routing firmware protocol is not supported.");
}

DriverEthInterfaceParams get_eth_interface_params() {
    UMD_THROW(error::RuntimeError, "The legacy Wormhole ETH routing firmware protocol is not supported.");
}

DriverNocParams get_noc_params() {
    // A Wormhole NOC address is 36 bits of local address followed by 6-bit X and Y coordinates.
    return {.noc_addr_local_bits = 36, .noc_addr_node_id_bits = 6};
}

}  // namespace wormhole

namespace {

bool is_selected(RiscType selection, RiscType flag) { return (selection & flag) != RiscType::NONE; }

}  // namespace

uint32_t WormholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    if (is_selected(risc_type, RiscType::ALL)) {
        risc_type |= RiscType::ALL_TENSIX;
    }
    if (is_selected(risc_type, RiscType::ALL_TRISCS)) {
        risc_type |= RiscType::ALL_TENSIX_TRISCS;
    }
    if (is_selected(risc_type, RiscType::ALL_DATA_MOVEMENT)) {
        risc_type |= RiscType::ALL_TENSIX_DMS;
    }

    uint32_t soft_reset_reg_value = 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::BRISC) ? wormhole::SOFT_RESET_BRISC : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::TRISC0) ? wormhole::SOFT_RESET_TRISC0 : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::TRISC1) ? wormhole::SOFT_RESET_TRISC1 : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::TRISC2) ? wormhole::SOFT_RESET_TRISC2 : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::NCRISC) ? wormhole::SOFT_RESET_NCRISC : 0;
    return soft_reset_reg_value;
}

RiscType WormholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    RiscType risc_type = RiscType::NONE;
    risc_type |= (soft_reset_reg_value & wormhole::SOFT_RESET_BRISC) ? RiscType::BRISC : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & wormhole::SOFT_RESET_TRISC0) ? RiscType::TRISC0 : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & wormhole::SOFT_RESET_TRISC1) ? RiscType::TRISC1 : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & wormhole::SOFT_RESET_TRISC2) ? RiscType::TRISC2 : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & wormhole::SOFT_RESET_NCRISC) ? RiscType::NCRISC : RiscType::NONE;
    return risc_type;
}

DeviceL1AddressParams WormholeImplementation::get_l1_address_params() const {
    return {wormhole::L1_BARRIER_BASE, wormhole::ERISC_BARRIER_BASE, wormhole::ETH_FW_VERSION_ADDR};
}

}  // namespace tt::umd
