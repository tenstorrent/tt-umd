// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "tt-umd/types/risc_type.hpp"

namespace tt::umd {

// Soft reset register bits of the classic Tensix RISCs (BRISC, three TRISCs, NCRISC), which Wormhole and
// Blackhole place differently from the RiscType flags.
struct TensixSoftResetBits {
    uint32_t brisc;
    uint32_t trisc0;
    uint32_t trisc1;
    uint32_t trisc2;
    uint32_t ncrisc;
};

// Register value selecting every RISC the flags name, directly or through ALL, ALL_TRISCS or ALL_DATA_MOVEMENT.
inline uint32_t tensix_soft_reset_reg_value(RiscType risc_type, const TensixSoftResetBits& bits) {
    const auto selects = [risc_type](RiscType mask) { return (risc_type & mask) != RiscType::NONE; };
    const RiscType any_trisc = RiscType::ALL | RiscType::ALL_TRISCS;
    const RiscType any_data_movement = RiscType::ALL | RiscType::ALL_DATA_MOVEMENT;

    uint32_t value = 0;
    value |= selects(any_data_movement | RiscType::BRISC) ? bits.brisc : 0;
    value |= selects(any_trisc | RiscType::TRISC0) ? bits.trisc0 : 0;
    value |= selects(any_trisc | RiscType::TRISC1) ? bits.trisc1 : 0;
    value |= selects(any_trisc | RiscType::TRISC2) ? bits.trisc2 : 0;
    value |= selects(any_data_movement | RiscType::NCRISC) ? bits.ncrisc : 0;
    return value;
}

// The individual RISCs whose bits are set in the register value.
inline RiscType tensix_soft_reset_risc_type(uint32_t reg_value, const TensixSoftResetBits& bits) {
    RiscType risc_type = RiscType::NONE;
    risc_type |= (reg_value & bits.brisc) ? RiscType::BRISC : RiscType::NONE;
    risc_type |= (reg_value & bits.trisc0) ? RiscType::TRISC0 : RiscType::NONE;
    risc_type |= (reg_value & bits.trisc1) ? RiscType::TRISC1 : RiscType::NONE;
    risc_type |= (reg_value & bits.trisc2) ? RiscType::TRISC2 : RiscType::NONE;
    risc_type |= (reg_value & bits.ncrisc) ? RiscType::NCRISC : RiscType::NONE;
    return risc_type;
}

}  // namespace tt::umd
