/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <string>

namespace tt::umd {

/**
 * @brief RiscType is an enum class that represents the different types of RISC cores on a single Tensix core.
 * @details RiscType contains both architecture agnostic and architecture specific options. It contains options
 * for each of the supported architectures. The flags have nothing to do with the specific soft reset register bits.
 */
enum class RiscType : std::uint32_t {
    // Both architectures have a common set of options for simpler usages, if you're not concerned about the specific
    // architecture.
    NONE = 0,
    ALL = 1 << 0,
    ALL_TRISCS = 1 << 1,
    ALL_DMS = 1 << 2,

    // The Tensix architecture has 1 triplet of TRISC cores, and two cores BRISC and NCRISC in overlay which act as data
    // movement cores.
    BRISC = 1 << 3,
    TRISC0 = 1 << 4,
    TRISC1 = 1 << 5,
    TRISC2 = 1 << 6,
    NCRISC = 1 << 7,

    // Combined constants
    ALL_TENSIX_TRISCS = TRISC0 | TRISC1 | TRISC2,
    ALL_TENSIX_DMS = BRISC | NCRISC,
    ALL_TENSIX = ALL_TENSIX_TRISCS | ALL_TENSIX_DMS,

    // The NEO Tensix architecture has 4 tripplets of TRISC cores, and 8 Data Movement cores.
    NEO0_TRISC0 = 1 << 8,
    NEO0_TRISC1 = 1 << 9,
    NEO0_TRISC2 = 1 << 10,
    NEO1_TRISC0 = 1 << 11,
    NEO1_TRISC1 = 1 << 12,
    NEO1_TRISC2 = 1 << 13,
    NEO2_TRISC0 = 1 << 14,
    NEO2_TRISC1 = 1 << 15,
    NEO2_TRISC2 = 1 << 16,
    NEO3_TRISC0 = 1 << 17,
    NEO3_TRISC1 = 1 << 18,
    NEO3_TRISC2 = 1 << 19,

    DM0 = 1 << 20,
    DM1 = 1 << 21,
    DM2 = 1 << 22,
    DM3 = 1 << 23,
    DM4 = 1 << 24,
    DM5 = 1 << 25,
    DM6 = 1 << 26,
    DM7 = 1 << 27,

    // Combined constants for each NEO triplet
    ALL_TRISCS_NEO0 = NEO0_TRISC0 | NEO0_TRISC1 | NEO0_TRISC2,
    ALL_TRISCS_NEO1 = NEO1_TRISC0 | NEO1_TRISC1 | NEO1_TRISC2,
    ALL_TRISCS_NEO2 = NEO2_TRISC0 | NEO2_TRISC1 | NEO2_TRISC2,
    ALL_TRISCS_NEO3 = NEO3_TRISC0 | NEO3_TRISC1 | NEO3_TRISC2,

    // Combined constants for all cores of each type
    ALL_TRISCS_NEO = NEO0_TRISC0 | NEO1_TRISC0 | NEO2_TRISC0 | NEO3_TRISC0,
    ALL_DMS_NEO = DM0 | DM1 | DM2 | DM3 | DM4 | DM5 | DM6 | DM7,
    ALL_NEO = ALL_TRISCS_NEO | ALL_DMS_NEO,
};

std::string RiscTypeToString(RiscType value);

RiscType invert_selected_options(RiscType selected);

constexpr RiscType operator|(RiscType lhs, RiscType rhs) {
    return static_cast<RiscType>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr RiscType operator&(RiscType lhs, RiscType rhs) {
    return static_cast<RiscType>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr bool operator!=(RiscType lhs, RiscType rhs) {
    return static_cast<uint32_t>(lhs) != static_cast<uint32_t>(rhs);
}

constexpr RiscType operator~(RiscType operand) { return static_cast<RiscType>(~static_cast<std::uint32_t>(operand)); }

constexpr RiscType& operator|=(RiscType& lhs, RiscType rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr RiscType& operator&=(RiscType& lhs, RiscType rhs) {
    lhs = lhs & rhs;
    return lhs;
}
}  // namespace tt::umd

namespace fmt {
template <>
struct formatter<tt::umd::RiscType> {
    constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

    template <typename Context>
    constexpr auto format(tt::umd::RiscType const& risc_type, Context& ctx) const {
        return format_to(ctx.out(), "{}", tt::umd::RiscTypeToString(risc_type));
    }
};
}  // namespace fmt
