/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Note: This is not yet used, and is not able to compile in the current state.
// But some implementation is given to illustrate some usage.

namespace tt::umd {

uint32_t grendel_implementation::get_soft_reset_reg_value(tt::umd::RiscType risc_type) const {
    if ((risc_type & RiscType::ALL_TENSIX) != RiscType::NONE) {
        // Throw if any of the old Tensix cores are selected.
        TT_THROW("Old Tensix risc cores should not be used on Grendel architecture.");
    }

    // Fill up Tensix related bits based on architecture agnostic bits.
    if ((risc_type & RiscType::ALL) != RiscType::NONE) {
        risc_type &= RiscType::ALL_NEO;
    }
    if ((risc_type & RiscType::ALL_TRISCS) != RiscType::NONE) {
        risc_type &= RiscType::ALL_NEO_TRISCS;
    }
    if ((risc_type & RiscType::ALL_DMS) != RiscType::NONE) {
        risc_type &= RiscType::ALL_NEO_DMS;
    }

    uint32_t soft_reset_reg_value = 0;
    if ((risc_type & RiscType::NEO0_TRISC0) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO0_TRISC0;
    }
    if ((risc_type & RiscType::NEO0_TRISC1) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO0_TRISC1;
    }
    if ((risc_type & RiscType::NEO0_TRISC2) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO0_TRISC2;
    }
    if ((risc_type & RiscType::NEO1_TRISC0) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO1_TRISC0;
    }
    if ((risc_type & RiscType::NEO1_TRISC1) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO1_TRISC1;
    }
    if ((risc_type & RiscType::NEO1_TRISC2) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO1_TRISC2;
    }
    if ((risc_type & RiscType::NEO2_TRISC0) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO2_TRISC0;
    }
    if ((risc_type & RiscType::NEO2_TRISC1) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO2_TRISC1;
    }
    if ((risc_type & RiscType::NEO2_TRISC2) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO2_TRISC2;
    }
    if ((risc_type & RiscType::NEO3_TRISC0) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO3_TRISC0;
    }
    if ((risc_type & RiscType::NEO3_TRISC1) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO3_TRISC1;
    }
    if ((risc_type & RiscType::NEO3_TRISC2) != RiscType::NONE) {
        soft_reset_reg_value |= grendel::SOFT_RESET_NEO3_TRISC2;
    }
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

    return soft_reset_reg_value;
}

}  // namespace tt::umd
