// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/core.h>
#include <fmt/format.h>

#include <cstdint>
#include <ostream>
#include <string>

namespace tt::umd {

/**
 * @brief RiscType is an enum class that represents the different types of RISC cores on a single Tensix core.
 * @details RiscType contains both architecture agnostic and architecture specific options. It contains options
 * for each of the supported architectures. The flags have nothing to do with the specific soft reset register bits.
 */
enum class RiscType : std::uint64_t {
    // Both architectures have a common set of options for simpler usages, if you're not concerned about the specific
    // architecture. The Data Movement equivalent cores for the tensix architecture include BRISC and NCRISC.
    NONE = 0,
    ALL = 1ULL << 0,
    ALL_TRISCS = 1ULL << 1,
    ALL_DATA_MOVEMENT = 1ULL << 2,

    // The Tensix architecture has 1 triplet of TRISC cores, and two cores BRISC and NCRISC in overlay which act as data
    // movement cores.
    BRISC = 1ULL << 3,
    TRISC0 = 1ULL << 4,
    TRISC1 = 1ULL << 5,
    TRISC2 = 1ULL << 6,
    NCRISC = 1ULL << 7,

    // Consider having separate entries for ETH and Tensix, so we don't overlap like this.
    ERISC0 = 1ULL << 3,
    ERISC1 = 1ULL << 4,

    // Combined constants.
    ALL_TENSIX_TRISCS = TRISC0 | TRISC1 | TRISC2,
    ALL_TENSIX_DMS = BRISC | NCRISC,
    ALL_TENSIX = ALL_TENSIX_TRISCS | ALL_TENSIX_DMS,

    // The NEO Tensix architecture has 4 tripplets of 4 TRISC cores each, and 8 Data Movement cores.
    NEO0_TRISC0 = 1ULL << 8,
    NEO0_TRISC1 = 1ULL << 9,
    NEO0_TRISC2 = 1ULL << 10,
    NEO0_TRISC3 = 1ULL << 11,

    NEO1_TRISC0 = 1ULL << 12,
    NEO1_TRISC1 = 1ULL << 13,
    NEO1_TRISC2 = 1ULL << 14,
    NEO1_TRISC3 = 1ULL << 15,

    NEO2_TRISC0 = 1ULL << 16,
    NEO2_TRISC1 = 1ULL << 17,
    NEO2_TRISC2 = 1ULL << 18,
    NEO2_TRISC3 = 1ULL << 19,

    NEO3_TRISC0 = 1ULL << 20,
    NEO3_TRISC1 = 1ULL << 21,
    NEO3_TRISC2 = 1ULL << 22,
    NEO3_TRISC3 = 1ULL << 23,

    DM0 = 1ULL << 24,
    DM1 = 1ULL << 25,
    DM2 = 1ULL << 26,
    DM3 = 1ULL << 27,
    DM4 = 1ULL << 28,
    DM5 = 1ULL << 29,
    DM6 = 1ULL << 30,
    DM7 = 1ULL << 31,

    // Maximum valid value - all bits 0-31.
    // This helps static analyzers understand the valid range for cast operations.
    VALID_BITS_MASK = 0xFFFFFFFFULL,

    // Combined constants for each NEO triplet.
    ALL_NEO0_TRISCS = NEO0_TRISC0 | NEO0_TRISC1 | NEO0_TRISC2 | NEO0_TRISC3,
    ALL_NEO1_TRISCS = NEO1_TRISC0 | NEO1_TRISC1 | NEO1_TRISC2 | NEO1_TRISC3,
    ALL_NEO2_TRISCS = NEO2_TRISC0 | NEO2_TRISC1 | NEO2_TRISC2 | NEO2_TRISC3,
    ALL_NEO3_TRISCS = NEO3_TRISC0 | NEO3_TRISC1 | NEO3_TRISC2 | NEO3_TRISC3,

    // Combined constants for all cores of each type.
    ALL_NEO_TRISCS = ALL_NEO0_TRISCS | ALL_NEO1_TRISCS | ALL_NEO2_TRISCS | ALL_NEO3_TRISCS,
    ALL_NEO_DMS = DM0 | DM1 | DM2 | DM3 | DM4 | DM5 | DM6 | DM7,
    ALL_NEO = ALL_NEO_TRISCS | ALL_NEO_DMS,
};

std::string RiscTypeToString(RiscType value);

RiscType invert_selected_options(RiscType selected);

constexpr RiscType operator|(RiscType lhs, RiscType rhs) {
    return static_cast<RiscType>(static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs));
}

constexpr RiscType operator&(RiscType lhs, RiscType rhs) {
    // Mask the result to ensure it's within valid range (bits 0-31).
    // This addresses Clang Static Analyzer optin.core.EnumCastOutOfRange.
    return static_cast<RiscType>(
        (static_cast<uint64_t>(lhs) & static_cast<uint64_t>(rhs)) & static_cast<uint64_t>(RiscType::VALID_BITS_MASK));
}

constexpr bool operator!=(RiscType lhs, RiscType rhs) {
    return static_cast<uint64_t>(lhs) != static_cast<uint64_t>(rhs);
}

constexpr RiscType operator~(RiscType operand) { return static_cast<RiscType>(~static_cast<std::uint64_t>(operand)); }

constexpr RiscType& operator|=(RiscType& lhs, RiscType rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr RiscType& operator&=(RiscType& lhs, RiscType rhs) {
    lhs = lhs & rhs;
    return lhs;
}

// Stream output operator for cout.
inline std::ostream& operator<<(std::ostream& os, RiscType risc_type) { return os << RiscTypeToString(risc_type); }

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
