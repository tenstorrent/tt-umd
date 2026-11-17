// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/wormhole_implementation.hpp"

#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

namespace wormhole {

DriverHostAddressParams get_host_address_params() { UMD_NOT_IMPLEMENTED(); }

DriverEthInterfaceParams get_eth_interface_params() { UMD_NOT_IMPLEMENTED(); }

DriverNocParams get_noc_params() { UMD_NOT_IMPLEMENTED(); }

}  // namespace wormhole

uint32_t WormholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const { UMD_NOT_IMPLEMENTED(); }

RiscType WormholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    UMD_NOT_IMPLEMENTED();
}

DeviceL1AddressParams WormholeImplementation::get_l1_address_params() const { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
