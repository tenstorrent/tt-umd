/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/format.h>

#include <cstdint>

#include "umd/device/tt_xy_pair.h"

// For documentation on Coordinate systems, lookup docs/coordinate_systems.md

/*
 * CoreType is an enum class that represents all types of cores
 * present on the Tenstorrent chip.
 */
// TODO: change to uint8_t and uplift to tt-metal
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
    // TODO: this keeps compatibility with existing code in SocDescriptor
    // but it won't be needed later on
    HARVESTED,
    ETH,
    WORKER,
    COUNT,
};

namespace tt::umd {
/*
 * CoordSystem is an enum class that represents all types of coordinate
 * systems that can be used to represent a core's location.
 */
enum class CoordSystem : std::uint8_t {
    LOGICAL,
    NOC0,
    VIRTUAL,
    TRANSLATED,
    NOC1,
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
        case CoreType::HARVESTED:
            return "HARVESTED";
        case CoreType::ETH:
            return "ETH";
        case CoreType::WORKER:
            return "WORKER";
        default:
            return "UNKNOWN";
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
        default:
            return "UNKNOWN";
    }
}

struct CoreCoord : public tt_xy_pair {
    CoreCoord() {}

    CoreCoord(const size_t x, const size_t y, const CoreType type, const CoordSystem coord_system) :
        tt_xy_pair(x, y), core_type(type), coord_system(coord_system) {}

    CoreCoord(const tt_xy_pair core, const CoreType type, const CoordSystem coord_system) :
        tt_xy_pair(core), core_type(type), coord_system(coord_system) {}

    CoreType core_type;
    CoordSystem coord_system;

    bool operator==(const CoreCoord& other) const {
        return this->x == other.x && this->y == other.y && this->core_type == other.core_type &&
               this->coord_system == other.coord_system;
    }

    bool operator<(const CoreCoord& o) const {
        if (x < o.x) {
            return true;
        }
        if (x > o.x) {
            return false;
        }
        if (y < o.y) {
            return true;
        }
        if (y > o.y) {
            return false;
        }
        if (core_type < o.core_type) {
            return true;
        }
        if (core_type > o.core_type) {
            return false;
        }
        return coord_system < o.coord_system;
    }

    std::string str() const {
        return "CoreCoord: (" + std::to_string(x) + ", " + std::to_string(y) + ", " + to_str(core_type) + ", " +
               to_str(coord_system) + ")";
    }
};

}  // namespace tt::umd

// TODO: To be removed once clients switch to namespace usage.
using tt::umd::CoordSystem;

namespace tt::umd {
// We can't define CoreType originally in the tt::umd namespace, due to a forward declaration in tt_metal.
using CoreType = ::CoreType;
}  // namespace tt::umd

namespace std {
template <>
struct hash<tt::umd::CoreCoord> {
    size_t operator()(const tt::umd::CoreCoord& core_coord) const {
        size_t seed = 0;
        seed = std::hash<size_t>{}(core_coord.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed = std::hash<size_t>{}(core_coord.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed = std::hash<tt::umd::CoreType>{}(core_coord.core_type) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed = std::hash<tt::umd::CoordSystem>{}(core_coord.coord_system) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
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
