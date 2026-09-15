// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/coordinates/att/att_map.hpp"

namespace tt::umd::att {

/**
 * A lone Mimir chiplet reached in LOCAL addressing, as served by the emu_axi command server.
 *
 * This map is synthetic, and that is the difference from grendel_qsr1_att_map.hpp. The qsr.s1 map
 * is transcribed from hardware: real ATT windows a Quasar programs, with endpoint tables the NOC
 * actually holds. A lone Mimir has no ATT and no package coordinates -- chippy reaches it flat
 * (`init_chiplets(metadata, use_spa_addressing_for_local_chiplet=false, false)`) and the server
 * wires the protocol straight to that chiplet's own AXI master. The windows below therefore
 * describe the chiplet's local address layout, expressed in the same structure so the emu backend
 * resolves through the mechanism every other backend uses rather than forking its own.
 *
 * Because the coordinates are UMD-side handles rather than NOC endpoints, the endpoint tables name
 * the cores of tests/soc_descs/mimir_1x1.yaml directly, in the order that file lists them.
 *
 * Bases come from chippy (lib/arch/grendel/mimir.h), which owns every address below the flat one:
 *   kMimirSmcLocalAddr      = 0x0          -> the config aperture base
 *   kMimirConfigSize        = 0x8000000    -> 128 MiB, so a 27-bit local field
 *   kMimirGddrDramLocalAddr = 0x800000000  -> the GDDR base
 * The DRAM slot width is the descriptor's own dram_bank_size (8 GiB, so a 33-bit local field);
 * chippy names one DRAM base in the local view rather than a base per GDDR tile.
 */

/**
 * The config aperture, one 128 MiB slot. endpoint_size 0: a single SMC needs no selector, so the
 * window spends every ignored bit on the local address and make_address() reduces to base | offset.
 */
inline constexpr Window MIMIR_1X1_FULL_TILE_WINDOW{
    .compare = 0x0ULL,
    .mask_bits = 27,
    .endpoint_shift = 27,
    .endpoint_size = 0,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

/** GDDR, one 8 GiB slot per DRAM core. Two cores, so a 1-bit selector above the 33-bit local field. */
inline constexpr Window MIMIR_1X1_DRAM_WINDOW{
    .compare = 0x800000000ULL,
    .mask_bits = 34,
    .endpoint_shift = 33,
    .endpoint_size = 1,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

/**
 * Mimir carries no compute, so no core may resolve through the worker window. It is left with an
 * empty endpoint table rather than a placeholder: Resolver builds its lookup from the table, so an
 * empty one makes every worker access fail with "no endpoint in this ATT map" instead of quietly
 * addressing something else.
 */
inline constexpr Window MIMIR_1X1_WORKER_WINDOW{
    .compare = 0x0ULL,
    .mask_bits = 0,
    .endpoint_shift = 0,
    .endpoint_size = 0,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

// clang-format off

/** The single SMC core, mimir_1x1.yaml `smc: [0-1]`. */
inline constexpr uint16_t MIMIR_1X1_FULL_TILE_ENDPOINT_WORDS[] = {
    0x040,  // (0, 1)
};

/** The two GDDR cores, in the order mimir_1x1.yaml lists them under `dram:`. */
inline constexpr uint16_t MIMIR_1X1_DRAM_ENDPOINT_WORDS[] = {
    0x000,  // (0, 0) -- channel 0
    0x001,  // (1, 0) -- channel 1
};

// clang-format on

inline constexpr MapData MIMIR_1X1_MAP{
    .windows =
        {
            MIMIR_1X1_WORKER_WINDOW,
            MIMIR_1X1_DRAM_WINDOW,
            MIMIR_1X1_FULL_TILE_WINDOW,
        },
    .endpoint_words =
        {
            Table<uint16_t>{},  // Worker: deliberately empty, see MIMIR_1X1_WORKER_WINDOW.
            Table<uint16_t>{MIMIR_1X1_DRAM_ENDPOINT_WORDS},
            Table<uint16_t>{MIMIR_1X1_FULL_TILE_ENDPOINT_WORDS},
        },
    .package_offset_x = 0,
    .package_offset_y = 0,
};

}  // namespace tt::umd::att
