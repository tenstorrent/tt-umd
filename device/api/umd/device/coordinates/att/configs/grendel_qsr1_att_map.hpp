// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/coordinates/att/att_map.hpp"

namespace tt::umd::att {

/**
 * The qsr.s1 ATT map, transcribed from the generated map the firmware programs:
 * models/qsr.s1/att/grendel_address_map_template.yaml in the grendelemulation repo, whose
 * `mask_table` gives the windows and whose `noc_endpoint_table` gives the endpoint tables.
 * Verified against the live register dump in models/qsr.s1/docs/qmk_att_dump.md.
 *
 * The expanded ranges in att/address_map.csv are a report generated from a separate, deliberately
 * frozen base and disagree with the programmed windows; they are not a source for these values.
 *
 * These values describe qsr.s1 only. Another Grendel model needs its own map, which is why the
 * resolver takes one rather than hardcoding it.
 */

// Mask-table entry 4: TensixNEO L1, one 16 MiB slot per NEO tile.
inline constexpr Window QSR1_WORKER_WINDOW{
    .compare = 0x10000000000ULL,
    .mask_bits = 30,
    .endpoint_shift = 24,
    .endpoint_size = 6,
    .endpoint_table_offset = 128,
    .translate_address = true,
};

// Mask-table entry 5: Mimir GDDR, one 8 GiB slot per D2D link.
inline constexpr Window QSR1_DRAM_WINDOW{
    .compare = 0x1000000000000ULL,
    .mask_bits = 38,
    .endpoint_shift = 33,
    .endpoint_size = 5,
    .endpoint_table_offset = 96,
    .translate_address = false,
};

// Mask-table entry 14: the per-tile config aperture, one 128 MiB slot per Quasar mesh tile.
inline constexpr Window QSR1_FULL_TILE_WINDOW{
    .compare = 0x1800000000ULL,
    .mask_bits = 33,
    .endpoint_shift = 27,
    .endpoint_size = 6,
    .endpoint_table_offset = 256,
    .translate_address = true,
};

// clang-format off

// Endpoint rows 128..159, the NEO grid in row-major order.
inline constexpr uint16_t QSR1_WORKER_ENDPOINT_WORDS[] = {
    0x104, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10a, 0x10b,
    0x144, 0x145, 0x146, 0x147, 0x148, 0x149, 0x14a, 0x14b,
    0x184, 0x185, 0x186, 0x187, 0x188, 0x189, 0x18a, 0x18b,
    0x1c4, 0x1c5, 0x1c6, 0x1c7, 0x1c8, 0x1c9, 0x1ca, 0x1cb,
};

// Endpoint rows 96..127. A Mimir fronts its GDDR over two D2D links and selector bit 4 picks
// between them, so the two tiles of one Mimir sit 16 selectors apart rather than side by side.
inline constexpr uint16_t QSR1_DRAM_ENDPOINT_WORDS[] = {
    0x246,  0x24a,  0x089,  0x085,  0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0x247,  0x24b,  0x088,  0x084,  0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
};

// Endpoint rows 256..315. The NEO grid fills the first 32 slots, then the perimeter: west column,
// north row, east column and south row descending, and the four corners last.
inline constexpr uint16_t QSR1_FULL_TILE_ENDPOINT_WORDS[] = {
    0x104, 0x105, 0x106, 0x107, 0x108, 0x109, 0x10a, 0x10b,
    0x144, 0x145, 0x146, 0x147, 0x148, 0x149, 0x14a, 0x14b,
    0x184, 0x185, 0x186, 0x187, 0x188, 0x189, 0x18a, 0x18b,
    0x1c4, 0x1c5, 0x1c6, 0x1c7, 0x1c8, 0x1c9, 0x1ca, 0x1cb,
    0x103, 0x143, 0x183, 0x1c3, 0x204, 0x205, 0x206, 0x207,
    0x208, 0x209, 0x20a, 0x20b, 0x1cc, 0x18c, 0x14c, 0x10c,
    0x0cb, 0x0ca, 0x0c9, 0x0c8, 0x0c7, 0x0c6, 0x0c5, 0x0c4,
    0x20c, 0x0cc, 0x203, 0x0c3,
};

// clang-format on

inline constexpr MapData GRENDEL_QSR1_MAP{
    .windows = {{QSR1_WORKER_WINDOW, QSR1_DRAM_WINDOW, QSR1_FULL_TILE_WINDOW}},
    .endpoint_words = {{QSR1_WORKER_ENDPOINT_WORDS, QSR1_DRAM_ENDPOINT_WORDS, QSR1_FULL_TILE_ENDPOINT_WORDS}},
    // The soc descriptor is drawn with the Quasar mesh origin at (1, 1), the POR frame puts it at (3, 3).
    .package_offset_x = 2,
    .package_offset_y = 2,
};

}  // namespace tt::umd::att
