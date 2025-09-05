// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/types/tensix_soft_reset_options.hpp"

#include "umd/device/cluster.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

std::string TensixSoftResetOptionsToString(TensixSoftResetOptions value) {
    std::string output;

    if ((value & TensixSoftResetOptions::BRISC) != TensixSoftResetOptions::NONE) {
        output += "BRISC | ";
    }
    if ((value & TensixSoftResetOptions::TRISC0) != TensixSoftResetOptions::NONE) {
        output += "TRISC0 | ";
    }
    if ((value & TensixSoftResetOptions::TRISC1) != TensixSoftResetOptions::NONE) {
        output += "TRISC1 | ";
    }
    if ((value & TensixSoftResetOptions::TRISC2) != TensixSoftResetOptions::NONE) {
        output += "TRISC2 | ";
    }
    if ((value & TensixSoftResetOptions::NCRISC) != TensixSoftResetOptions::NONE) {
        output += "NCRISC | ";
    }
    if ((value & TensixSoftResetOptions::STAGGERED_START) != TensixSoftResetOptions::NONE) {
        output += "STAGGERED_START | ";
    }

    if (output.empty()) {
        output = "UNKNOWN";
    } else {
        output.erase(output.end() - 3, output.end());
    }

    return output;
}

TensixSoftResetOptions invert_selected_options(TensixSoftResetOptions selected) {
    uint32_t selected_bits = static_cast<uint32_t>(selected);
    uint32_t inverted = (~selected_bits) & static_cast<uint32_t>(ALL_TENSIX_SOFT_RESET);
    return static_cast<TensixSoftResetOptions>(inverted);
}

}  // namespace tt::umd
