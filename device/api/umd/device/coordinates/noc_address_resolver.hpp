// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

/**
 * Resolves a target core plus a core-local offset into the single flat address a backend must
 * issue to reach it.
 *
 * Most backends do not need this: on Wormhole/Blackhole the destination core travels out of band
 * (in a TLB config register), so the address on the wire stays core-local. Grendel/Quasar is the
 * opposite -- the NOC Address Translation Table (ATT) resolves a *flat* 64-bit address into a
 * destination NOC (x, y) plus a local address, so the driver has to flatten the coordinate into
 * the address before issuing the access.
 *
 * The interface takes a CoreCoord rather than a tt_xy_pair on purpose: the window a core belongs
 * to follows from its CoreType (L1 for Tensix, GDDR for DRAM, config otherwise), and inferring
 * that from coordinate ranges instead is ambiguous -- on qsr.s1 the south D2D ingress rows
 * collide with legitimate config-window tiles.
 *
 * Implementations must be stateless with respect to a single access (pure function of the
 * arguments), so they are safe to call concurrently under the device lock.
 */
class NocAddressResolver {
public:
    virtual ~NocAddressResolver() = default;

    /**
     * Translate a target core and core-local offset into the flat address to issue.
     *
     * @param core Destination core. Any coord system is accepted; the implementation converts as
     *             needed. core_type selects the address window, so it must be set.
     * @param offset Core-local byte offset within the target's own address space.
     * @param noc_id NOC the access is issued on. Grendel resolves the system NOC (SMN) through a
     *               different set of windows than NOC0.
     * @return Flat address the backend should issue.
     */
    virtual uint64_t to_flat_address(const CoreCoord& core, uint64_t offset, NocId noc_id) const = 0;
};

}  // namespace tt::umd
