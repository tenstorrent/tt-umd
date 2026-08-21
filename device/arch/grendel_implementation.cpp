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

tlb_configuration GrendelImplementation::get_tlb_configuration(uint32_t tlb_index) const {
    // If TLB index is in range for 4GB tlbs (8 TLBs after 202 TLBs for 2MB).
    if (tlb_index >= grendel::TLB_COUNT_2M && tlb_index < grendel::TLB_COUNT_2M + grendel::TLB_COUNT_4G) {
        return tlb_configuration{
            .size = grendel::DYNAMIC_TLB_4G_SIZE,
            .base = grendel::DYNAMIC_TLB_4G_BASE,
            .cfg_addr = grendel::DYNAMIC_TLB_4G_CFG_ADDR,
            .index_offset = tlb_index - grendel::TLB_BASE_INDEX_4G,
            .tlb_offset =
                grendel::DYNAMIC_TLB_4G_BASE + (tlb_index - grendel::TLB_BASE_INDEX_4G) * grendel::DYNAMIC_TLB_4G_SIZE,
            .offset = grendel::TLB_4G_OFFSET,
        };
    }

    return tlb_configuration{
        .size = grendel::DYNAMIC_TLB_2M_SIZE,
        .base = grendel::DYNAMIC_TLB_2M_BASE,
        .cfg_addr = grendel::DYNAMIC_TLB_2M_CFG_ADDR,
        .index_offset = tlb_index - grendel::TLB_BASE_INDEX_2M,
        .tlb_offset =
            grendel::DYNAMIC_TLB_2M_BASE + (tlb_index - grendel::TLB_BASE_INDEX_2M) * grendel::DYNAMIC_TLB_2M_SIZE,
        .offset = grendel::TLB_2M_OFFSET,
    };
}

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
