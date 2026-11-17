// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/wormhole_implementation.hpp"

#include <cstdint>

#include "arch/tensix_soft_reset.hpp"
#include "common/unsupported.hpp"
#include "tt-umd/types/wormhole_eth.hpp"
#include "tt-umd/types/wormhole_l1.hpp"

namespace tt::umd {

namespace wormhole {

DriverHostAddressParams get_host_address_params() { UMD_NOT_SUPPORTED("Wormhole remote communication"); }

DriverEthInterfaceParams get_eth_interface_params() { UMD_NOT_SUPPORTED("Wormhole remote communication"); }

DriverNocParams get_noc_params() { UMD_NOT_SUPPORTED("Wormhole remote communication"); }

}  // namespace wormhole

namespace {

constexpr TensixSoftResetBits SOFT_RESET_BITS = {
    wormhole::SOFT_RESET_BRISC,
    wormhole::SOFT_RESET_TRISC0,
    wormhole::SOFT_RESET_TRISC1,
    wormhole::SOFT_RESET_TRISC2,
    wormhole::SOFT_RESET_NCRISC};

}  // namespace

uint32_t WormholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    return tensix_soft_reset_reg_value(risc_type, SOFT_RESET_BITS);
}

RiscType WormholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    return tensix_soft_reset_risc_type(soft_reset_reg_value, SOFT_RESET_BITS);
}

DeviceL1AddressParams WormholeImplementation::get_l1_address_params() const {
    return {
        .tensix_l1_barrier_base = wormhole::L1_BARRIER_BASE,
        .eth_l1_barrier_base = wormhole::ERISC_BARRIER_BASE,
        .fw_version_addr = wormhole::ETH_FW_VERSION_ADDR};
}

}  // namespace tt::umd
