// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/blackhole_implementation.hpp"

#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

namespace blackhole {

tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1) { UMD_NOT_IMPLEMENTED(); }

}  // namespace blackhole

uint32_t BlackholeImplementation::get_soft_reset_reg_value(RiscType risc_type) const { UMD_NOT_IMPLEMENTED(); }

RiscType BlackholeImplementation::get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const {
    UMD_NOT_IMPLEMENTED();
}

DeviceL1AddressParams BlackholeImplementation::get_l1_address_params() const { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
