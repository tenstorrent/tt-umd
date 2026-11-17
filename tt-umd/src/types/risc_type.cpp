// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/types/risc_type.hpp"

#include <string>
#include <utility>

namespace tt::umd {

namespace {

bool has_any(RiscType value, RiscType flags) { return (value & flags) != RiscType::NONE; }

// Every single-bit flag with its name. ERISC0/ERISC1 share bits with BRISC/TRISC0, so they print
// under the Tensix names.
constexpr std::pair<RiscType, const char*> RISC_TYPE_NAMES[] = {
    {RiscType::ALL, "ALL"},
    {RiscType::ALL_TRISCS, "ALL_TRISCS"},
    {RiscType::ALL_DATA_MOVEMENT, "ALL_DATA_MOVEMENT"},
    {RiscType::BRISC, "BRISC"},
    {RiscType::TRISC0, "TRISC0"},
    {RiscType::TRISC1, "TRISC1"},
    {RiscType::TRISC2, "TRISC2"},
    {RiscType::NCRISC, "NCRISC"},
    {RiscType::NEO0_TRISC0, "NEO0_TRISC0"},
    {RiscType::NEO0_TRISC1, "NEO0_TRISC1"},
    {RiscType::NEO0_TRISC2, "NEO0_TRISC2"},
    {RiscType::NEO0_TRISC3, "NEO0_TRISC3"},
    {RiscType::NEO1_TRISC0, "NEO1_TRISC0"},
    {RiscType::NEO1_TRISC1, "NEO1_TRISC1"},
    {RiscType::NEO1_TRISC2, "NEO1_TRISC2"},
    {RiscType::NEO1_TRISC3, "NEO1_TRISC3"},
    {RiscType::NEO2_TRISC0, "NEO2_TRISC0"},
    {RiscType::NEO2_TRISC1, "NEO2_TRISC1"},
    {RiscType::NEO2_TRISC2, "NEO2_TRISC2"},
    {RiscType::NEO2_TRISC3, "NEO2_TRISC3"},
    {RiscType::NEO3_TRISC0, "NEO3_TRISC0"},
    {RiscType::NEO3_TRISC1, "NEO3_TRISC1"},
    {RiscType::NEO3_TRISC2, "NEO3_TRISC2"},
    {RiscType::NEO3_TRISC3, "NEO3_TRISC3"},
    {RiscType::DM0, "DM0"},
    {RiscType::DM1, "DM1"},
    {RiscType::DM2, "DM2"},
    {RiscType::DM3, "DM3"},
    {RiscType::DM4, "DM4"},
    {RiscType::DM5, "DM5"},
    {RiscType::DM6, "DM6"},
    {RiscType::DM7, "DM7"},
    {RiscType::ALL_NEO_DMS_UNCORE, "ALL_NEO_DMS_UNCORE"},
    {RiscType::NEO_DM_UNCORE, "NEO_DM_UNCORE"},
};

}  // namespace

std::string RiscTypeToString(RiscType value) {
    if (value == RiscType::NONE) {
        return "NONE";
    }

    std::string result;
    for (const auto& [flag, name] : RISC_TYPE_NAMES) {
        if (has_any(value, flag)) {
            if (!result.empty()) {
                result += " | ";
            }
            result += name;
        }
    }
    return result;
}

RiscType invert_selected_options(RiscType selected) {
    // The complement is taken within the core family the selection belongs to; a selection made only
    // of architecture-agnostic flags is read as a Tensix one.
    const bool is_neo = has_any(selected, RiscType::ALL_NEO | RiscType::ALL_NEO_DMS_UNCORE | RiscType::NEO_DM_UNCORE);
    const RiscType family = is_neo ? RiscType::ALL_NEO : RiscType::ALL_TENSIX;

    RiscType expanded = selected;
    if (has_any(selected, RiscType::ALL)) {
        expanded |= family;
    }
    if (has_any(selected, RiscType::ALL_TRISCS)) {
        expanded |= is_neo ? RiscType::ALL_NEO_TRISCS : RiscType::ALL_TENSIX_TRISCS;
    }
    if (has_any(selected, RiscType::ALL_DATA_MOVEMENT)) {
        expanded |= is_neo ? RiscType::ALL_NEO_DMS : RiscType::ALL_TENSIX_DMS;
    }

    return family & ~expanded;
}

}  // namespace tt::umd
