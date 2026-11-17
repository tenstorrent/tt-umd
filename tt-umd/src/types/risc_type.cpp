// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/types/risc_type.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace tt::umd {

namespace {

constexpr bool has_any(RiscType value, RiscType mask) { return (value & mask) != RiscType::NONE; }

// Named flags, combined constants first, so a value is spelled with the fewest names. ERISC0 and ERISC1
// share their bits with BRISC and TRISC0 and are spelled as those.
constexpr std::array<std::pair<RiscType, const char *>, 44> RISC_TYPE_NAMES = {{
    {RiscType::ALL, "ALL"},
    {RiscType::ALL_TRISCS, "ALL_TRISCS"},
    {RiscType::ALL_DATA_MOVEMENT, "ALL_DATA_MOVEMENT"},
    {RiscType::ALL_TENSIX, "ALL_TENSIX"},
    {RiscType::ALL_TENSIX_TRISCS, "ALL_TENSIX_TRISCS"},
    {RiscType::ALL_TENSIX_DMS, "ALL_TENSIX_DMS"},
    {RiscType::BRISC, "BRISC"},
    {RiscType::TRISC0, "TRISC0"},
    {RiscType::TRISC1, "TRISC1"},
    {RiscType::TRISC2, "TRISC2"},
    {RiscType::NCRISC, "NCRISC"},
    {RiscType::ALL_NEO, "ALL_NEO"},
    {RiscType::ALL_NEO_TRISCS, "ALL_NEO_TRISCS"},
    {RiscType::ALL_NEO_DMS, "ALL_NEO_DMS"},
    {RiscType::ALL_NEO0_TRISCS, "ALL_NEO0_TRISCS"},
    {RiscType::ALL_NEO1_TRISCS, "ALL_NEO1_TRISCS"},
    {RiscType::ALL_NEO2_TRISCS, "ALL_NEO2_TRISCS"},
    {RiscType::ALL_NEO3_TRISCS, "ALL_NEO3_TRISCS"},
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
}};

}  // namespace

std::string RiscTypeToString(RiscType value) {
    if (value == RiscType::NONE) {
        return "NONE";
    }
    std::string result;
    RiscType remaining = value;
    for (const auto &[flag, name] : RISC_TYPE_NAMES) {
        if ((remaining & flag) == flag) {
            result += result.empty() ? "" : "|";
            result += name;
            remaining &= ~flag;
        }
    }
    if (remaining != RiscType::NONE) {
        result += result.empty() ? "" : "|";
        result += fmt::format("0x{:x}", static_cast<uint64_t>(remaining));
    }
    return result;
}

RiscType invert_selected_options(RiscType selected) {
    // A selection is inverted within the family of cores it names: NEO, classic Tensix, or the
    // architecture-agnostic groups.
    if (has_any(selected, RiscType::ALL_NEO)) {
        return ~selected & RiscType::ALL_NEO;
    }
    if (has_any(selected, RiscType::ALL_TENSIX)) {
        return ~selected & RiscType::ALL_TENSIX;
    }
    const RiscType all_groups = RiscType::ALL_TRISCS | RiscType::ALL_DATA_MOVEMENT;
    const RiscType selected_groups = has_any(selected, RiscType::ALL) ? all_groups : selected & all_groups;
    const RiscType inverted = ~selected_groups & all_groups;
    return inverted == all_groups ? RiscType::ALL : inverted;
}

}  // namespace tt::umd
