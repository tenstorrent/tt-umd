// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/grendel_implementation.hpp"

#include <array>
#include <utility>

#include "tt-umd/types/blackhole_eth.hpp"
#include "tt-umd/types/blackhole_l1.hpp"

namespace tt::umd {

namespace grendel {

tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1) {
    const tt_xy_pair arc_core_noc0 = ARC_CORES_NOC0[0];
    if (use_noc1 && !noc_translation_enabled) {
        return {NOC0_X_TO_NOC1_X[arc_core_noc0.x], NOC0_Y_TO_NOC1_Y[arc_core_noc0.y]};
    }
    return arc_core_noc0;
}

}  // namespace grendel

namespace {

bool is_selected(RiscType selection, RiscType flag) { return (selection & flag) != RiscType::NONE; }

constexpr std::array<std::pair<RiscType, uint32_t>, 8> DM_RESET_BITS = {{
    {RiscType::DM0, grendel::SOFT_RESET_DM0},
    {RiscType::DM1, grendel::SOFT_RESET_DM1},
    {RiscType::DM2, grendel::SOFT_RESET_DM2},
    {RiscType::DM3, grendel::SOFT_RESET_DM3},
    {RiscType::DM4, grendel::SOFT_RESET_DM4},
    {RiscType::DM5, grendel::SOFT_RESET_DM5},
    {RiscType::DM6, grendel::SOFT_RESET_DM6},
    {RiscType::DM7, grendel::SOFT_RESET_DM7},
}};

// The register has one set of TRISC bits, shared by the TRISC of the same index in every NEO.
constexpr std::array<std::pair<RiscType, uint32_t>, 4> TRISC_RESET_BITS = {{
    {RiscType::NEO0_TRISC0 | RiscType::NEO1_TRISC0 | RiscType::NEO2_TRISC0 | RiscType::NEO3_TRISC0,
     grendel::SOFT_RESET_TRISC0},
    {RiscType::NEO0_TRISC1 | RiscType::NEO1_TRISC1 | RiscType::NEO2_TRISC1 | RiscType::NEO3_TRISC1,
     grendel::SOFT_RESET_TRISC1},
    {RiscType::NEO0_TRISC2 | RiscType::NEO1_TRISC2 | RiscType::NEO2_TRISC2 | RiscType::NEO3_TRISC2,
     grendel::SOFT_RESET_TRISC2},
    {RiscType::NEO0_TRISC3 | RiscType::NEO1_TRISC3 | RiscType::NEO2_TRISC3 | RiscType::NEO3_TRISC3,
     grendel::SOFT_RESET_TRISC3},
}};

}  // namespace

uint32_t GrendelImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    if (is_selected(risc_type, RiscType::ALL)) {
        risc_type |= RiscType::ALL_NEO;
    }
    if (is_selected(risc_type, RiscType::ALL_TRISCS)) {
        risc_type |= RiscType::ALL_NEO_TRISCS;
    }
    if (is_selected(risc_type, RiscType::ALL_DATA_MOVEMENT)) {
        risc_type |= RiscType::ALL_NEO_DMS;
    }

    uint32_t soft_reset_reg_value = 0;
    for (const auto& [flags, bit] : DM_RESET_BITS) {
        soft_reset_reg_value |= is_selected(risc_type, flags) ? bit : 0;
    }
    for (const auto& [flags, bit] : TRISC_RESET_BITS) {
        soft_reset_reg_value |= is_selected(risc_type, flags) ? bit : 0;
    }
    return soft_reset_reg_value;
}

RiscType GrendelImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    // The TRISC bits overlap the DM bits, so the register value is read back as data movement cores.
    RiscType risc_type = RiscType::NONE;
    for (const auto& [flag, bit] : DM_RESET_BITS) {
        risc_type |= (soft_reset_reg_value & bit) ? flag : RiscType::NONE;
    }
    return risc_type;
}

DeviceL1AddressParams GrendelImplementation::get_l1_address_params() const {
    return {blackhole::L1_BARRIER_BASE, blackhole::ERISC_BARRIER_BASE, blackhole::ETH_FW_VERSION_ADDR};
}

}  // namespace tt::umd
