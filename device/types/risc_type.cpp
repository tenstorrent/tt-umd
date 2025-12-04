// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/types/risc_type.hpp"

namespace tt::umd {

std::string RiscTypeToString(RiscType value) {
    std::string output;

    if ((value & RiscType::ALL) != RiscType::NONE) {
        output += "ALL | ";
    }
    if ((value & RiscType::ALL_TRISCS) != RiscType::NONE) {
        output += "ALL_TRISCS | ";
    }
    if ((value & RiscType::ALL_DATA_MOVEMENT) != RiscType::NONE) {
        output += "ALL_DATA_MOVEMENT | ";
    }

    if ((value & RiscType::BRISC) != RiscType::NONE) {
        output += "BRISC | ";
    }
    if ((value & RiscType::TRISC0) != RiscType::NONE) {
        output += "TRISC0 | ";
    }
    if ((value & RiscType::TRISC1) != RiscType::NONE) {
        output += "TRISC1 | ";
    }
    if ((value & RiscType::TRISC2) != RiscType::NONE) {
        output += "TRISC2 | ";
    }
    if ((value & RiscType::NCRISC) != RiscType::NONE) {
        output += "NCRISC | ";
    }

    // Check NEO Tensix TRISC cores
    if ((value & RiscType::NEO0_TRISC0) != RiscType::NONE) {
        output += "NEO0_TRISC0 | ";
    }
    if ((value & RiscType::NEO0_TRISC1) != RiscType::NONE) {
        output += "NEO0_TRISC1 | ";
    }
    if ((value & RiscType::NEO0_TRISC2) != RiscType::NONE) {
        output += "NEO0_TRISC2 | ";
    }
    if ((value & RiscType::NEO0_TRISC3) != RiscType::NONE) {
        output += "NEO0_TRISC3 | ";
    }
    if ((value & RiscType::NEO1_TRISC0) != RiscType::NONE) {
        output += "NEO1_TRISC0 | ";
    }
    if ((value & RiscType::NEO1_TRISC1) != RiscType::NONE) {
        output += "NEO1_TRISC1 | ";
    }
    if ((value & RiscType::NEO1_TRISC2) != RiscType::NONE) {
        output += "NEO1_TRISC2 | ";
    }
    if ((value & RiscType::NEO1_TRISC3) != RiscType::NONE) {
        output += "NEO1_TRISC3 | ";
    }
    if ((value & RiscType::NEO2_TRISC0) != RiscType::NONE) {
        output += "NEO2_TRISC0 | ";
    }
    if ((value & RiscType::NEO2_TRISC1) != RiscType::NONE) {
        output += "NEO2_TRISC1 | ";
    }
    if ((value & RiscType::NEO2_TRISC2) != RiscType::NONE) {
        output += "NEO2_TRISC2 | ";
    }
    if ((value & RiscType::NEO2_TRISC3) != RiscType::NONE) {
        output += "NEO2_TRISC3 | ";
    }
    if ((value & RiscType::NEO3_TRISC0) != RiscType::NONE) {
        output += "NEO3_TRISC0 | ";
    }
    if ((value & RiscType::NEO3_TRISC1) != RiscType::NONE) {
        output += "NEO3_TRISC1 | ";
    }
    if ((value & RiscType::NEO3_TRISC2) != RiscType::NONE) {
        output += "NEO3_TRISC2 | ";
    }

    // Check NEO Tensix Data Movement cores
    if ((value & RiscType::DM0) != RiscType::NONE) {
        output += "DM0 | ";
    }
    if ((value & RiscType::DM1) != RiscType::NONE) {
        output += "DM1 | ";
    }
    if ((value & RiscType::DM2) != RiscType::NONE) {
        output += "DM2 | ";
    }
    if ((value & RiscType::DM3) != RiscType::NONE) {
        output += "DM3 | ";
    }
    if ((value & RiscType::DM4) != RiscType::NONE) {
        output += "DM4 | ";
    }
    if ((value & RiscType::DM5) != RiscType::NONE) {
        output += "DM5 | ";
    }
    if ((value & RiscType::DM6) != RiscType::NONE) {
        output += "DM6 | ";
    }
    if ((value & RiscType::DM7) != RiscType::NONE) {
        output += "DM7 | ";
    }

    if (output.empty()) {
        output = "NONE";
    } else {
        // Remove the trailing " | "
        output.erase(output.end() - 3, output.end());
    }

    return output;
}

RiscType invert_selected_options(RiscType selected) {
    uint32_t selected_bits = static_cast<uint32_t>(selected);
    uint32_t inverted =
        (~selected_bits) & static_cast<uint32_t>(RiscType::ALL | RiscType::ALL_TENSIX | RiscType::ALL_NEO);
    return static_cast<RiscType>(inverted);
}

}  // namespace tt::umd
