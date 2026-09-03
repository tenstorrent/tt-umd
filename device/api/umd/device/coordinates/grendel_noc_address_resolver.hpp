// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/coordinates/noc_address_resolver.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

class SocDescriptor;

/**
 * The Quasar NOC ATT global address windows, plus the mesh geometry needed to index into them.
 *
 * Each window is a base plus a power-of-two stride, and the ATT recovers the index by pure bit
 * slicing: it compares addr[63:mask] against the window base, takes the endpoint id from
 * addr[mask-1:ep_idx], and passes addr[ep_idx-1:0] through as the core-local address. So a window's
 * addressing granule is exactly `1 << ep_idx`, which is why validate() requires power-of-two
 * strides -- a stride the ATT cannot express silently targets the wrong tile.
 *
 * Provenance of the values below, from the qsr.s1 golden map's mask table
 * (models/qsr.s1/att/grendel_address_map_template.yaml in the grendelemulation repo):
 *
 *   window        compare            ep_idx  ep_id_size  table_offset  granule
 *   TensixNEO L1  0x10000000000      24      6           128           16 MiB  = stride
 *   config        0x1800000000       28      6           256           256 MiB = stride
 *   DDR large     0x1000000000000    32      6           120           4 GiB   = stride / 2
 *
 * Note the DDR case: the granule is one *D2D lane*, and a Mimir spans two, so dram_stride is
 * 2 granules (8 GiB per Mimir) and the lane is selected by the high bit of the offset within it.
 * That is also why both of a Mimir's lane coordinates map to the same channel.
 *
 * Indices are row-major over the relevant grid, matching the firmware generator's window expansion
 * (py/grendel_fw_gen/address_map.py in the grendelemulation repo).
 *
 * Coordinates are NOC0, which for the Grendel models is the *package* NoC frame -- the same
 * 1-based frame as topology.yaml, the generated address map's noc_x/noc_y columns, and the soc
 * descriptor in tt-umd-simulators (emu/qsr-s1-t6x1_DM/qsr-s1-t6x1_DM_emu.yaml).
 *
 * The package frame is not a preference: it is the only frame that can express every core. The
 * south Mimir D2D ingress row sits at package y=0, which has no representation once the quasar
 * origin is subtracted. See docs/GLOBAL_ADDRESSING.md section 2 in the grendelemulation repo.
 * Window indices are still counted from the mesh/grid origin, so the origin is subtracted here
 * rather than by the caller.
 *
 * The defaults describe **qsr.s1 specifically**, and are hardcoded here deliberately rather than
 * read from the soc descriptor: they are the same species of Grendel constant as the TLB offsets and
 * core tables in grendel_implementation.hpp, and a mismatch against the firmware's ATT programming
 * is caught by the emulation tests (an address matching no mask entry goes nowhere; a bad index
 * mismatches on readback) rather than corrupting silently.
 *
 * Do not assume they generalise. The qsr.s1 map is a *modified* copy of the DV template -- its DDR
 * mask ep-select fields were widened for the two-lane D2D split -- whereas models/quasar points at
 * the unmodified upstream map. A second Grendel model therefore needs its own table, which is why
 * GrendelNocAddressResolver takes one rather than hardcoding it internally.
 */
struct GrendelAddressWindows {
    // TensixNEO L1, 16 MiB per NEO tile, indexed row-major over the NEO grid.
    uint64_t neo_l1_base = 0x10000000000ULL;
    uint64_t neo_l1_stride = 0x1000000ULL;

    // Per-tile config aperture, 256 MiB per tile, indexed row-major over the full Quasar mesh.
    uint64_t config_base = 0x1800000000ULL;
    uint64_t config_stride = 0x10000000ULL;

    // Mimir GDDR, large (8 GiB) window, indexed by DRAM channel.
    uint64_t dram_base = 0x1000000000000ULL;
    uint64_t dram_stride = 0x200000000ULL;

    // Package-frame coordinate of the Quasar mesh origin (topology.yaml placement.quasar_origin).
    uint32_t quasar_origin_x = 1;
    uint32_t quasar_origin_y = 1;

    // Full Quasar mesh width, used as the row stride for the config window index.
    //
    // NOTE: this is the *mesh* width (10), not the soc descriptor's grid.x_size. That grid is a
    // bounding box over everything drawn in the package -- it is 12 wide because it also spans the
    // Keraunos column at x=11 -- and using it here would skew every config index by two tiles.
    uint32_t mesh_x_size = 10;
    uint32_t mesh_y_size = 6;

    // NEO grid origin and extent, in package coordinates.
    uint32_t neo_x_start = 2;
    uint32_t neo_y_start = 2;
    uint32_t neo_x_count = 8;
    uint32_t neo_y_count = 4;

    /** Whether package (x, y) falls inside the NEO grid, and so has an L1 window. */
    bool is_in_neo_grid(uint32_t noc0_x, uint32_t noc0_y) const;

    /** Flat address of a NEO tile's L1 at a byte offset. Requires is_in_neo_grid(). */
    uint64_t tensix_l1_address(uint32_t noc0_x, uint32_t noc0_y, uint64_t offset) const;

    /** Flat address of any mesh tile's config aperture at a register offset. */
    uint64_t config_address(uint32_t noc0_x, uint32_t noc0_y, uint64_t offset) const;

    /** Whether package (x, y) is inside the Quasar mesh, and so has a config aperture. */
    bool is_in_mesh(uint32_t noc0_x, uint32_t noc0_y) const;

    /** Flat address within a DRAM channel's GDDR window. */
    uint64_t dram_address(uint32_t channel, uint64_t offset) const;

    /** Throw if the table is internally inconsistent (non power-of-two stride, empty grid). */
    void validate() const;
};

/**
 * Grendel/Quasar NocAddressResolver: selects an ATT global window from the core's CoreType and
 * flattens its coordinate into that window's index.
 *
 * DRAM channels come from the SocDescriptor rather than being derived from coordinates, so the
 * descriptor's own DRAM channel map stays the single source of truth for which GDDR window a core
 * belongs to.
 */
class GrendelNocAddressResolver : public NocAddressResolver {
public:
    /**
     * @param soc_descriptor Descriptor used for coordinate translation and DRAM channel lookup.
     *                       Must outlive this resolver.
     * @param windows Address window table. Validated at construction.
     */
    explicit GrendelNocAddressResolver(const SocDescriptor& soc_descriptor, const GrendelAddressWindows& windows = {});

    uint64_t to_flat_address(const CoreCoord& core, uint64_t offset, NocId noc_id) const override;

    const GrendelAddressWindows& get_windows() const { return windows_; }

private:
    const SocDescriptor* soc_descriptor_;
    GrendelAddressWindows windows_;
};

}  // namespace tt::umd
