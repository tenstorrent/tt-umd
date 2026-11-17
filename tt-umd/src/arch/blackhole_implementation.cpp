// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/blackhole_implementation.hpp"

#include <cstdint>

#include "arch/tensix_soft_reset.hpp"
#include "tt-umd/types/blackhole_eth.hpp"
#include "tt-umd/types/blackhole_l1.hpp"

namespace tt::umd {

namespace blackhole {

tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1) {
    // With translation enabled the ARC keeps its NOC0 coordinates on both NOCs.
    const tt_xy_pair arc_core = ARC_CORES_NOC0[0];
    if (noc_translation_enabled || !use_noc1) {
        return arc_core;
    }
    return {NOC0_X_TO_NOC1_X[arc_core.x], NOC0_Y_TO_NOC1_Y[arc_core.y]};
}

}  // namespace blackhole

namespace {

constexpr TensixSoftResetBits SOFT_RESET_BITS = {
    blackhole::SOFT_RESET_BRISC,
    blackhole::SOFT_RESET_TRISC0,
    blackhole::SOFT_RESET_TRISC1,
    blackhole::SOFT_RESET_TRISC2,
    blackhole::SOFT_RESET_NCRISC};

}  // namespace

uint32_t BlackholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    return tensix_soft_reset_reg_value(risc_type, SOFT_RESET_BITS);
}

RiscType BlackholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    return tensix_soft_reset_risc_type(soft_reset_reg_value, SOFT_RESET_BITS);
}

DeviceL1AddressParams BlackholeImplementation::get_l1_address_params() const {
    return {
        .tensix_l1_barrier_base = blackhole::L1_BARRIER_BASE,
        .eth_l1_barrier_base = blackhole::ERISC_BARRIER_BASE,
        .fw_version_addr = blackhole::ETH_FW_VERSION_ADDR};
}

}  // namespace tt::umd
