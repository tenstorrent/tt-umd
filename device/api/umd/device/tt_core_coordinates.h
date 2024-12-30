/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

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
    // TODO: this keeps compatibility with existing code in SocDescriptor
    // but it won't be needed later on
    HARVESTED,
    ETH,
    WORKER,
};

/*
 * CoordSystem is an enum class that represents all types of coordinate
 * systems that can be used to represent a core's location.
 */
enum class CoordSystem : std::uint8_t {
    LOGICAL,
    PHYSICAL,
    VIRTUAL,
    TRANSLATED,
};

static inline std::string to_str(CoreType core_type) {
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

static inline std::string to_str(CoordSystem coord_system) {
    switch (coord_system) {
        case CoordSystem::LOGICAL:
            return "LOGICAL";
        case CoordSystem::PHYSICAL:
            return "PHYSICAL";
        case CoordSystem::VIRTUAL:
            return "VIRTUAL";
        case CoordSystem::TRANSLATED:
            return "TRANSLATED";
        default:
            return "UNKNOWN";
    }
}

namespace tt::umd {

struct CoreCoord : public tt_xy_pair {
    CoreCoord() {}

    CoreCoord(const size_t x, const size_t y, const CoreType type, const CoordSystem coord_system) :
        tt_xy_pair(x, y), core_type(type), coord_system(coord_system) {}

    CoreCoord(tt_xy_pair core, const CoreType type, const CoordSystem coord_system) :
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
};

static inline std::string to_str(const CoreCoord& core_coord) {
    return "CoreCoord: (" + std::to_string(core_coord.x) + ", " + std::to_string(core_coord.y) + ", " +
           to_str(core_coord.core_type) + ", " + to_str(core_coord.coord_system) + ")";
};

}  // namespace tt::umd

namespace std {
template <>
struct hash<tt::umd::CoreCoord> {
    size_t operator()(const tt::umd::CoreCoord& core_coord) const;
};
}  // namespace std
