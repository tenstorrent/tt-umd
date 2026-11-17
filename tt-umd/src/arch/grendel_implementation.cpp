// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/grendel_implementation.hpp"

#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

namespace grendel {

tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1) {
    // With translation enabled the ARC keeps its NOC0 coordinates on both NOCs.
    const tt_xy_pair arc_core = ARC_CORES_NOC0[0];
    if (noc_translation_enabled || !use_noc1) {
        return arc_core;
    }
    return {NOC0_X_TO_NOC1_X[arc_core.x], NOC0_Y_TO_NOC1_Y[arc_core.y]};
}

}  // namespace grendel

uint32_t GrendelImplementation::get_soft_reset_reg_value(RiscType risc_type) const {
    UMD_NOT_SUPPORTED("Grendel RISC reset");
}

RiscType GrendelImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    UMD_NOT_SUPPORTED("Grendel RISC reset");
}

DeviceL1AddressParams GrendelImplementation::get_l1_address_params() const {
    UMD_NOT_SUPPORTED("Grendel L1 address map");
}

}  // namespace tt::umd
