// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <sstream>

#include "umd/device/types/xy_pair.hpp"

// For documentation on Coordinate systems, lookup docs/coordinate_systems.md.

// Types in this file can be used without using the driver, hence they aren't in tt::umd namespace.
namespace tt {
/*
 * CoreType is an enum class that represents all types of cores
 * present on the Tenstorrent chip.
 */
// TODO: change to uint8_t and uplift to tt-metal.
enum class CoreType {
    ARC,
    DRAM,
    ACTIVE_ETH,
    IDLE_ETH,
    PCIE,
    TENSIX,
    ROUTER_ONLY,
    SECURITY,
    L2CPU,
    DISPATCH,
    // TODO: this keeps compatibility with existing code in SocDescriptor
    // but it won't be needed later on
    HARVESTED,
    ETH,
    WORKER,
    COUNT,
    UNSPECIFIED,
};

/*
 * CoordSystem is an enum class that represents all types of coordinate
 * systems that can be used to represent a core's location.
 */
enum class CoordSystem : std::uint8_t {
    LOGICAL,
    NOC0,
    TRANSLATED,
    NOC1,
    LITERAL,  // LITERAL is not an actual coordinate system. It just means not to perform any operations on the
              // coordinates.
};

static inline std::string to_str(const CoreType core_type) {
    switch (core_type) {
        case CoreType::ARC:
            return "ARC";
        case CoreType::DRAM:
            return "DRAM";
        case CoreType::ACTIVE_ETH:
            return "ACTIVE_ETH";
        case CoreType::IDLE_ETH:
            return "IDLE_ETH";
        case CoreType::PCIE:
            return "PCIE";
        case CoreType::TENSIX:
            return "TENSIX";
        case CoreType::ROUTER_ONLY:
            return "ROUTER_ONLY";
        case CoreType::SECURITY:
            return "SECURITY";
        case CoreType::L2CPU:
            return "L2CPU";
        case CoreType::DISPATCH:
            return "DISPATCH";
        case CoreType::HARVESTED:
            return "HARVESTED";
        case CoreType::ETH:
            return "ETH";
        case CoreType::WORKER:
            return "WORKER";
        case CoreType::UNSPECIFIED:
            return "UNSPECIFIED";
        default:
            return "UNKNOWN";
    }
}

// Core type shorthands are used in stringifying CoreCoord.
static inline char type_shorthand(const CoreType type) {
    switch (type) {
        case CoreType::ARC:
            return 'a';
        case CoreType::DRAM:
            return 'd';
        case CoreType::ACTIVE_ETH:
        case CoreType::IDLE_ETH:
        case CoreType::ETH:
            return 'e';
        case CoreType::PCIE:
            return 'p';
        case CoreType::TENSIX:
        case CoreType::WORKER:
            return 't';
        case CoreType::ROUTER_ONLY:
            return 'r';
        case CoreType::SECURITY:
            return 's';
        case CoreType::L2CPU:
            return 'l';
        case CoreType::UNSPECIFIED:
            return '\0';
        default:
            return '?';
    }
}

static inline std::string to_str(const CoordSystem coord_system) {
    switch (coord_system) {
        case CoordSystem::LOGICAL:
            return "LOGICAL";
        case CoordSystem::NOC0:
            return "NOC0";
        case CoordSystem::TRANSLATED:
            return "TRANSLATED";
        case CoordSystem::NOC1:
            return "NOC1";
        case CoordSystem::LITERAL:
            return "LITERAL";
        default:
            return "UNKNOWN";
    }
}

// TODO: There is a conflicting declaration in tt_metal for CoreCoord. We need to remove that one before we can move
// this CoreCoord to tt namespace.
namespace umd {
struct CoreCoord {
    CoreCoord() = default;

    constexpr CoreCoord(
        const size_t x,
        const size_t y,
        const CoreType type = CoreType::UNSPECIFIED,
        const CoordSystem coord_system = CoordSystem::LITERAL) :
        x(x), y(y), core_type(type), coord_system(coord_system) {}

    constexpr CoreCoord(
        const xy_pair core,
        const CoreType type = CoreType::UNSPECIFIED,
        const CoordSystem coord_system = CoordSystem::LITERAL) :
        x(core.x), y(core.y), core_type(type), coord_system(coord_system) {}

    size_t x = 0;
    size_t y = 0;
    CoreType core_type = CoreType::UNSPECIFIED;
    CoordSystem coord_system = CoordSystem::LITERAL;

    xy_pair to_pair() const { return xy_pair{x, y}; }

    bool operator==(const CoreCoord& other) const {
        return this->x == other.x && this->y == other.y && this->core_type == other.core_type &&
               this->coord_system == other.coord_system;
    }

    bool operator<(const CoreCoord& o) const {
        if (x != o.x) {
            return x < o.x;
        }
        if (y != o.y) {
            return y < o.y;
        }
        if (core_type != o.core_type) {
            return core_type < o.core_type;
        }
        return coord_system < o.coord_system;
    }

    std::string str() const {
        if (coord_system == CoordSystem::LITERAL) {
            return xy_pair{x, y}.str();
        }
        std::stringstream ss;
        char shorthand = type_shorthand(core_type);
        if (shorthand != '\0') {
            ss << shorthand;
        }
        ss << x << '-' << y;
        ss << ' ' << '(' << to_str(coord_system) << ')';
        return ss.str();
    }
};

// Comparison operators for CoreCoord with xy_pair to resolve ambiguity.
constexpr bool operator==(const umd::CoreCoord& a, const xy_pair& b) { return a.x == b.x && a.y == b.y; }

constexpr bool operator==(const xy_pair& a, const umd::CoreCoord& b) { return a.x == b.x && a.y == b.y; }

constexpr bool operator!=(const umd::CoreCoord& a, const xy_pair& b) { return !(a == b); }

constexpr bool operator!=(const xy_pair& a, const umd::CoreCoord& b) { return !(a == b); }

}  // namespace umd

}  // namespace tt

namespace std {
template <>
struct hash<tt::umd::CoreCoord> {
    size_t operator()(const tt::umd::CoreCoord& core_coord) const {
        size_t seed = 0;
        seed = std::hash<size_t>{}(core_coord.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed = std::hash<size_t>{}(core_coord.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed = std::hash<tt::CoreType>{}(core_coord.core_type) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed = std::hash<tt::CoordSystem>{}(core_coord.coord_system) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
}  // namespace std

namespace fmt {
template <>
struct formatter<tt::umd::CoreCoord> {
    constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }

    template <typename Context>
    constexpr auto format(tt::umd::CoreCoord const& core_coord, Context& ctx) const {
        return format_to(ctx.out(), "{}", core_coord.str());
    }
};
}  // namespace fmt
