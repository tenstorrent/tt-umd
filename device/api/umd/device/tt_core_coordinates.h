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
enum class CoreType : uint8_t {
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
};

/*
 * CoordSystem is an enum class that represents all types of coordinate
 * systems that can be used to represent a core's location.
 */
enum class CoordSystem : uint8_t {
    LOGICAL,
    PHYSICAL,
    VIRTUAL,
    TRANSLATED,
};

namespace tt::umd {

struct CoreCoord : public tt_xy_pair {
    CoreCoord() {}

    CoreCoord(const size_t x, const size_t y, const CoreType type, const CoordSystem coord_system) :
        tt_xy_pair(x, y), core_type(type), coord_system(coord_system) {}

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

}  // namespace tt::umd

namespace std {
template <>
struct hash<tt::umd::CoreCoord> {
    size_t operator()(const tt::umd::CoreCoord& core_coord) const;
};
}  // namespace std
