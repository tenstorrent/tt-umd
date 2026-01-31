// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <ostream>

#include "umd/device/utils/common.hpp"

// Types in this file can be used without using the driver, hence they aren't in tt::umd namespace.
namespace tt {

/**
 * Enums for different architectures.
 */
enum class ARCH {
    WORMHOLE_B0 = 2,
    BLACKHOLE = 3,
    QUASAR = 4,
    Invalid = 0xFF
};

static inline tt::ARCH arch_from_str(const std::string &arch_str) {
    std::string arch_str_lower = to_lower(arch_str);

    if ((arch_str_lower == "wormhole") || (arch_str_lower == "wormhole_b0")) {
        return tt::ARCH::WORMHOLE_B0;
    } else if (arch_str_lower == "blackhole") {
        return tt::ARCH::BLACKHOLE;
    } else if (arch_str_lower == "quasar") {
        return tt::ARCH::QUASAR;
    } else {
        return tt::ARCH::Invalid;
    }
}

static inline std::string arch_to_str(const tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return "wormhole_b0";
        case tt::ARCH::BLACKHOLE:
            return "blackhole";
        case tt::ARCH::QUASAR:
            return "quasar";
        case tt::ARCH::Invalid:
        default:
            return "Invalid";
    }
}

static inline std::ostream &operator<<(std::ostream &out, const tt::ARCH &arch) { return out << arch_to_str(arch); }

}  // namespace tt

namespace fmt {
template <>
struct formatter<tt::ARCH> {
    constexpr auto parse(fmt::format_parse_context &ctx) { return ctx.begin(); }

    template <typename Context>
    constexpr auto format(tt::ARCH const &arch, Context &ctx) const {
        return format_to(ctx.out(), "{}", tt::arch_to_str(arch));
    }
};
}  // namespace fmt
