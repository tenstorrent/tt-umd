// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arch/blackhole_implementation.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <tuple>

#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/blackhole_l1.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

DeviceL1AddressParams BlackholeImplementation::get_l1_address_params() const {
    // L1 barrier base and erisc barrier base should be explicitly set by the client.
    // Setting some default values here, but it should be ultimately overridden by the client.
    return {blackhole::L1_BARRIER_BASE, blackhole::ERISC_BARRIER_BASE, blackhole::ETH_FW_VERSION_ADDR};
}

uint32_t BlackholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    if ((risc_type & RiscType::ALL_NEO) != RiscType::NONE) {
        // Throw if any of the NEO cores are selected.
        UMD_THROW(error::RuntimeError, "NEO risc cores should not be used on Blackhole architecture.");
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
        soft_reset_reg_value |= blackhole::SOFT_RESET_BRISC;
    }
    if ((risc_type & RiscType::TRISC0) != RiscType::NONE) {
        soft_reset_reg_value |= blackhole::SOFT_RESET_TRISC0;
    }
    if ((risc_type & RiscType::TRISC1) != RiscType::NONE) {
        soft_reset_reg_value |= blackhole::SOFT_RESET_TRISC1;
    }
    if ((risc_type & RiscType::TRISC2) != RiscType::NONE) {
        soft_reset_reg_value |= blackhole::SOFT_RESET_TRISC2;
    }
    if ((risc_type & RiscType::NCRISC) != RiscType::NONE) {
        soft_reset_reg_value |= blackhole::SOFT_RESET_NCRISC;
    }

    return soft_reset_reg_value;
}

RiscType BlackholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    RiscType risc_type = RiscType::NONE;
    if (soft_reset_reg_value & blackhole::SOFT_RESET_BRISC) {
        risc_type |= RiscType::BRISC;
    }
    if (soft_reset_reg_value & blackhole::SOFT_RESET_TRISC0) {
        risc_type |= RiscType::TRISC0;
    }
    if (soft_reset_reg_value & blackhole::SOFT_RESET_TRISC1) {
        risc_type |= RiscType::TRISC1;
    }
    if (soft_reset_reg_value & blackhole::SOFT_RESET_TRISC2) {
        risc_type |= RiscType::TRISC2;
    }
    if (soft_reset_reg_value & blackhole::SOFT_RESET_NCRISC) {
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

namespace blackhole {
tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1) {
    return (noc_translation_enabled || !use_noc1) ? blackhole::ARC_CORES_NOC0[0]
                                                  : tt_xy_pair(
                                                        blackhole::NOC0_X_TO_NOC1_X[blackhole::ARC_CORES_NOC0[0].x],
                                                        blackhole::NOC0_Y_TO_NOC1_Y[blackhole::ARC_CORES_NOC0[0].y]);
}
}  // namespace blackhole

}  // namespace tt::umd
