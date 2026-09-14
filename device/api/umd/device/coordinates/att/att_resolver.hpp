// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

#include "umd/device/coordinates/att/att_map.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd::att {

/**
 * Resolves a target core and a core-local offset into the single flat address the Quasar NOC ATT
 * needs to reach it.
 *
 * Wormhole and Blackhole carry the destination core out of band in a TLB config register, leaving
 * the address on the wire core-local. Quasar is the opposite: the ATT resolves a flat address into
 * a destination core plus a local address, so the driver folds the coordinate into the address.
 */
class Resolver {
public:
    /** @param map Window and selector tables for the target product. */
    explicit Resolver(const MapData& map);

    /**
     * Flat address reaching @p core at core-local byte offset @p offset for a transfer of
     * @p size bytes.
     *
     * @param core Target core, in the frame the soc descriptor uses.
     * @param core_type Selects the window. Inferring it from the coordinate instead is ambiguous:
     *                  the D2D ingress rows overlap coordinates that legitimately belong to
     *                  another window.
     * @throws error::RuntimeError if the core has no window in this map, or the transfer would
     *         run past the target's slot.
     */
    uint64_t resolve(tt_xy_pair core, CoreType core_type, uint64_t offset, uint64_t size) const;

private:
    MapData map_;

    /** Each window's endpoint words inverted into a coordinate lookup, built once at construction. */
    std::array<std::unordered_map<uint16_t, uint32_t>, WINDOW_CLASS_COUNT> selectors_;
};

}  // namespace tt::umd::att
