// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/blackhole_implementation.hpp"

#include "tt-umd/types/blackhole_eth.hpp"
#include "tt-umd/types/blackhole_l1.hpp"

namespace tt::umd {

namespace blackhole {

tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1) {
    const tt_xy_pair arc_core_noc0 = ARC_CORES_NOC0[0];
    // ARC sits in the X = 8 column, which translation leaves at its NOC0 coordinates on both NOCs. Only
    // untranslated NOC1 traffic needs the mirrored NOC1 coordinate.
    if (use_noc1 && !noc_translation_enabled) {
        return {NOC0_X_TO_NOC1_X[arc_core_noc0.x], NOC0_Y_TO_NOC1_Y[arc_core_noc0.y]};
    }
    return arc_core_noc0;
}

}  // namespace blackhole

namespace {

bool is_selected(RiscType selection, RiscType flag) { return (selection & flag) != RiscType::NONE; }

}  // namespace

uint32_t BlackholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
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
    soft_reset_reg_value |= is_selected(risc_type, RiscType::BRISC) ? blackhole::SOFT_RESET_BRISC : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::TRISC0) ? blackhole::SOFT_RESET_TRISC0 : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::TRISC1) ? blackhole::SOFT_RESET_TRISC1 : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::TRISC2) ? blackhole::SOFT_RESET_TRISC2 : 0;
    soft_reset_reg_value |= is_selected(risc_type, RiscType::NCRISC) ? blackhole::SOFT_RESET_NCRISC : 0;
    return soft_reset_reg_value;
}

RiscType BlackholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    RiscType risc_type = RiscType::NONE;
    risc_type |= (soft_reset_reg_value & blackhole::SOFT_RESET_BRISC) ? RiscType::BRISC : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & blackhole::SOFT_RESET_TRISC0) ? RiscType::TRISC0 : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & blackhole::SOFT_RESET_TRISC1) ? RiscType::TRISC1 : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & blackhole::SOFT_RESET_TRISC2) ? RiscType::TRISC2 : RiscType::NONE;
    risc_type |= (soft_reset_reg_value & blackhole::SOFT_RESET_NCRISC) ? RiscType::NCRISC : RiscType::NONE;
    return risc_type;
}

DeviceL1AddressParams BlackholeImplementation::get_l1_address_params() const {
    return {blackhole::L1_BARRIER_BASE, blackhole::ERISC_BARRIER_BASE, blackhole::ETH_FW_VERSION_ADDR};
}

}  // namespace tt::umd
