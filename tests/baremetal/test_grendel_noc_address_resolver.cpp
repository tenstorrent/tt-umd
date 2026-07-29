// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

#include "umd/device/coordinates/grendel_noc_address_resolver.hpp"

using namespace tt;
using namespace tt::umd;

// Golden vectors below are taken verbatim from the generated qsr.s1 address map in the
// grendelemulation repo (models/qsr.s1/att/address_map.csv), which is emitted by the same firmware
// generator that programs the ATT. Its noc_x/noc_y columns are package coordinates, which is also
// the frame the resolver takes and the frame the soc descriptor in tt-umd-simulators declares
// (emu/qsr-s1-t6x1_DM/qsr-s1-t6x1_DM_emu.yaml) -- so these cases need no conversion.
//
// They exist to fail loudly if UMD's window table ever drifts from the firmware's golden map -- a
// drift there silently targets the wrong tile rather than erroring.

namespace {

// Defaults describe the qsr.s1 chiplet family: origin (1,1), 10x6 mesh, NEO grid (2,2)-(9,5).
GrendelAddressWindows qsr_s1_windows() { return GrendelAddressWindows{}; }

}  // namespace

TEST(GrendelNocAddressResolver, DefaultWindowTableIsSelfConsistent) { EXPECT_NO_THROW(qsr_s1_windows().validate()); }

// address_map.csv, space=TensixNeo: (noc_x, noc_y) -> start_address.
TEST(GrendelNocAddressResolver, TensixL1MatchesGoldenAddressMap) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    // tile_id 0 -- first NEO tile, and the only populated one in t6x1.
    EXPECT_EQ(windows.tensix_l1_address(2, 2, 0), 0x10000000000ULL);
    // tile_id 1 -- next column
    EXPECT_EQ(windows.tensix_l1_address(3, 2, 0), 0x10001000000ULL);
    // tile_id 8 -- next row, so the index advances by the 8-wide NEO grid
    EXPECT_EQ(windows.tensix_l1_address(2, 3, 0), 0x10008000000ULL);
    // tile_id 31 -- last NEO tile
    EXPECT_EQ(windows.tensix_l1_address(9, 5, 0), 0x1001f000000ULL);
}

// address_map.csv, space=Config chiplet=Quasar[0]: tile_id counts row-major from the mesh origin.
TEST(GrendelNocAddressResolver, ConfigApertureMatchesGoldenAddressMap) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    // tile_id 0 -- the SMU, which is also the mesh origin
    EXPECT_EQ(windows.config_address(1, 1, 0), 0x1800000000ULL);
    // tile_id 10 -- one full mesh row on
    EXPECT_EQ(windows.config_address(1, 2, 0), 0x18a0000000ULL);
    // tile_id 21
    EXPECT_EQ(windows.config_address(2, 3, 0), 0x1950000000ULL);
    // tile_id 59 -- last mesh tile
    EXPECT_EQ(windows.config_address(10, 6, 0), 0x1bb0000000ULL);
}

TEST(GrendelNocAddressResolver, DramWindowIsIndexedByChannel) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    EXPECT_EQ(windows.dram_address(0, 0), 0x1000000000000ULL);
    // 8 GiB per Mimir.
    EXPECT_EQ(windows.dram_address(1, 0), 0x1000200000000ULL);
    EXPECT_EQ(windows.dram_address(3, 0), 0x1000600000000ULL);
}

TEST(GrendelNocAddressResolver, OffsetIsAddedWithinTheWindowGranule) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    EXPECT_EQ(windows.tensix_l1_address(2, 2, 0x1234), 0x10000001234ULL);
    EXPECT_EQ(windows.config_address(1, 1, 0x2010000), 0x1802010000ULL);
    EXPECT_EQ(windows.dram_address(1, 0x40), 0x1000200000040ULL);

    // Largest offset that still lands in the same slot.
    EXPECT_EQ(windows.tensix_l1_address(2, 2, windows.neo_l1_stride - 1), 0x10000ffffffULL);
}

// An offset at or beyond the stride would carry into the next slot and hit a different core. That
// is the silent-misrouting failure this component exists to prevent, so it must throw.
TEST(GrendelNocAddressResolver, OffsetBeyondWindowGranuleThrows) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    EXPECT_THROW(windows.tensix_l1_address(2, 2, windows.neo_l1_stride), std::exception);
    EXPECT_THROW(windows.config_address(1, 1, windows.config_stride), std::exception);
    EXPECT_THROW(windows.dram_address(0, windows.dram_stride), std::exception);
}

TEST(GrendelNocAddressResolver, NeoGridBoundsAreHalfOpen) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    EXPECT_TRUE(windows.is_in_neo_grid(2, 2));
    EXPECT_TRUE(windows.is_in_neo_grid(9, 5));
    // The SMU and the mesh edge rings are not NEO tiles.
    EXPECT_FALSE(windows.is_in_neo_grid(1, 1));
    EXPECT_FALSE(windows.is_in_neo_grid(10, 5));
    EXPECT_FALSE(windows.is_in_neo_grid(9, 6));

    EXPECT_THROW(windows.tensix_l1_address(1, 1, 0), std::exception);
}

// Cores the soc descriptor declares outside the Quasar mesh -- the Keraunos column at package x=11
// and the Mimir D2D ingress rows at package y=0 / y=7 -- have no config slot, and must not silently
// alias onto a Quasar tile.
TEST(GrendelNocAddressResolver, CoreOutsideMeshHasNoConfigAperture) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    EXPECT_TRUE(windows.is_in_mesh(1, 1));
    EXPECT_TRUE(windows.is_in_mesh(10, 6));

    // Keraunos tiles, listed as router_only in the descriptor at x=11.
    EXPECT_FALSE(windows.is_in_mesh(11, 2));
    EXPECT_THROW(windows.config_address(11, 2, 0), std::exception);

    // North and south Mimir D2D ingress rows.
    EXPECT_FALSE(windows.is_in_mesh(4, 7));
    EXPECT_THROW(windows.config_address(4, 7, 0), std::exception);
    EXPECT_FALSE(windows.is_in_mesh(7, 0));
    EXPECT_THROW(windows.config_address(7, 0, 0), std::exception);
}

// stride == 1 << ep_idx is an ATT invariant, not a convention: the hardware recovers the window
// index by slicing address bits, so a non-power-of-two stride cannot be expressed at all.
TEST(GrendelNocAddressResolver, NonPowerOfTwoStrideIsRejected) {
    GrendelAddressWindows windows = qsr_s1_windows();
    windows.config_stride = 0x30000000;
    EXPECT_THROW(windows.validate(), std::exception);

    windows = qsr_s1_windows();
    windows.neo_l1_stride = 0;
    EXPECT_THROW(windows.validate(), std::exception);
}

TEST(GrendelNocAddressResolver, EmptyGridIsRejected) {
    GrendelAddressWindows windows = qsr_s1_windows();
    windows.mesh_x_size = 0;
    EXPECT_THROW(windows.validate(), std::exception);

    windows = qsr_s1_windows();
    windows.neo_x_count = 0;
    EXPECT_THROW(windows.validate(), std::exception);
}

// Regression guard for the frame bug this component replaces. py/umd/remote.py keyed its DRAM
// lookup on package-frame ingress coordinates while addressing NEO/config in the physical frame
// (package minus the origin). Mixing them makes package (8,1) -- a real bottom-row mesh tile --
// arrive as physical (7,0), which collides with the south Mimir ingress coordinate (7,0) and
// misroutes a config access into Mimir m2's GDDR window.
//
// In a single package frame the two rows cannot collide: the south ingress row is y=0 and the
// bottom mesh row is y=1, so they are simply different coordinates.
TEST(GrendelNocAddressResolver, MeshAndDramIngressRowsDoNotCollideInPackageFrame) {
    const GrendelAddressWindows windows = qsr_s1_windows();

    // The real bottom-row mesh tile resolves to its own config slot (tile_id 7).
    EXPECT_TRUE(windows.is_in_mesh(8, 1));
    const uint64_t config = windows.config_address(8, 1, 0);
    EXPECT_EQ(config, windows.config_base + 7 * windows.config_stride);
    EXPECT_LT(config, windows.dram_base);

    // The south ingress coordinate is a distinct point that is not a mesh tile at all.
    EXPECT_FALSE(windows.is_in_mesh(7, 0));
}
