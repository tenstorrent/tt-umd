// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arch/grendel_implementation.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <tuple>

#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/blackhole_l1.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

DeviceL1AddressParams GrendelImplementation::get_l1_address_params() const {
    // L1 barrier base and erisc barrier base should be explicitly set by the client.
    // Setting some default values here, but it should be ultimately overridden by the client.
    return {blackhole::L1_BARRIER_BASE, blackhole::ERISC_BARRIER_BASE, blackhole::ETH_FW_VERSION_ADDR};
}

uint32_t GrendelImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    if ((risc_type & RiscType::ALL_TENSIX) != RiscType::NONE) {
        // Throw if any of the NEO cores are selected.
        UMD_THROW(error::RuntimeError, "TENSIX risc cores should not be used on Grendel architecture.");
    }

    // Fill up Tensix related bits based on architecture agnostic bits.
    if ((risc_type & RiscType::ALL) != RiscType::NONE) {
        risc_type |= RiscType::ALL_NEO;
    }
    if ((risc_type & RiscType::ALL_TRISCS) != RiscType::NONE) {
        risc_type |= RiscType::ALL_NEO_TRISCS;
    }
    if ((risc_type & RiscType::ALL_DATA_MOVEMENT) != RiscType::NONE) {
        risc_type |= RiscType::ALL_NEO_DMS;
    }

    uint32_t soft_reset_reg_value = 0;
    if ((risc_type & RiscType::DM0) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM0;
    }
    if ((risc_type & RiscType::DM1) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM1;
    }
    if ((risc_type & RiscType::DM2) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM2;
    }
    if ((risc_type & RiscType::DM3) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM3;
    }
    if ((risc_type & RiscType::DM4) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM4;
    }
    if ((risc_type & RiscType::DM5) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM5;
    }
    if ((risc_type & RiscType::DM6) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM6;
    }
    if ((risc_type & RiscType::DM7) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_DM7;
    }
    if ((risc_type & RiscType::ALL_NEO0_TRISCS) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_TRISC0;
    }
    if ((risc_type & RiscType::ALL_NEO1_TRISCS) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_TRISC1;
    }
    if ((risc_type & RiscType::ALL_NEO2_TRISCS) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_TRISC2;
    }
    if ((risc_type & RiscType::ALL_NEO3_TRISCS) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_TRISC3;
    }

    return soft_reset_reg_value;
}

RiscType GrendelImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    RiscType risc_type = RiscType::NONE;
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM0) {
        risc_type |= RiscType::DM0;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM1) {
        risc_type |= RiscType::DM1;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM2) {
        risc_type |= RiscType::DM2;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM3) {
        risc_type |= RiscType::DM3;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM4) {
        risc_type |= RiscType::DM4;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM5) {
        risc_type |= RiscType::DM5;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM6) {
        risc_type |= RiscType::DM6;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_DM7) {
        risc_type |= RiscType::DM7;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_TRISC0) {
        risc_type |= RiscType::ALL_NEO0_TRISCS;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_TRISC1) {
        risc_type |= RiscType::ALL_NEO1_TRISCS;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_TRISC2) {
        risc_type |= RiscType::ALL_NEO2_TRISCS;
    }
    if (soft_reset_reg_value & grendel::SOFT_RESET_TRISC3) {
        risc_type |= RiscType::ALL_NEO3_TRISCS;
    }

    // Set arhitecture agnostic bits based on tensix bits.
    if ((risc_type & RiscType::ALL_NEO) != RiscType::NONE) {
        risc_type |= RiscType::ALL;
    }
    if ((risc_type & RiscType::ALL_NEO_TRISCS) != RiscType::NONE) {
        risc_type |= RiscType::ALL_TRISCS;
    }
    if ((risc_type & RiscType::ALL_NEO_DMS) != RiscType::NONE) {
        risc_type |= RiscType::ALL_DATA_MOVEMENT;
    }

    return risc_type;
}

namespace grendel {
tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1) {
    return (noc_translation_enabled || !use_noc1) ? grendel::ARC_CORES_NOC0[0]
                                                  : tt_xy_pair(
                                                        grendel::NOC0_X_TO_NOC1_X[grendel::ARC_CORES_NOC0[0].x],
                                                        grendel::NOC0_Y_TO_NOC1_Y[grendel::ARC_CORES_NOC0[0].y]);
}
}  // namespace grendel

}  // namespace tt::umd
