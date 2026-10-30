// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "tt-umd/coordinates/att/att_map.hpp"

namespace tt::umd::att {

/**
 * Two Horizon 2x3 devices behind linked crossbars (aether horizon_x2), split by column: column P of
 * device d is a device of its own, which only its NOC2AXI (P,2) maps, in that NOC2AXI's crossbar
 * range ((d + 1) << 36) + (P << 32). Transcribed from tests/att_maps/horizon_x2_columns.yaml, the
 * tables the testbench programs (AETHER_ATT_CONFIG), and checked against it.
 *
 * A column device's soc descriptor is 1x3 at x = 0, and package_offset_x moves it to column P.
 * Endpoint words are (y << 6) | x in the tables' frame: Tensix (P,0), Dispatch (P,1) and NOC2AXI
 * (P,2), which fronts DRAM bank P.
 */

// The rows of both the L1 unicast window, 4 MB per tile (its whole L1), and the CSR window, 128 MB
// per tile (its whole map).
inline constexpr uint16_t HORIZON_X2_COLUMN0_WORDS[] = {0x000, 0x040, 0x080};
inline constexpr uint16_t HORIZON_X2_COLUMN1_WORDS[] = {0x001, 0x041, 0x081};

// DRAM bank P, reached straight through the crossbar's dram_P range, which the NOC2AXI does not
// translate.
inline constexpr uint16_t HORIZON_X2_COLUMN0_DRAM_WORDS[] = {0x080};
inline constexpr uint16_t HORIZON_X2_COLUMN1_DRAM_WORDS[] = {0x081};

inline constexpr Window HORIZON_X2_DEV0_COLUMN0_L1_WINDOW{
    .compare = 0x10'4000'0000,
    .mask_bits = 24,
    .endpoint_shift = 22,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_COLUMN0_CSR_WINDOW{
    .compare = 0x10'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_COLUMN0_DRAM_WINDOW{
    .compare = 0x13'0000'0000,
    .mask_bits = 30,
    .endpoint_shift = 30,
    .endpoint_size = 0,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

inline constexpr Window HORIZON_X2_DEV0_COLUMN1_L1_WINDOW{
    .compare = 0x11'4000'0000,
    .mask_bits = 24,
    .endpoint_shift = 22,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_COLUMN1_CSR_WINDOW{
    .compare = 0x11'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_COLUMN1_DRAM_WINDOW{
    .compare = 0x13'4000'0000,
    .mask_bits = 30,
    .endpoint_shift = 30,
    .endpoint_size = 0,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

inline constexpr Window HORIZON_X2_DEV1_COLUMN0_L1_WINDOW{
    .compare = 0x20'4000'0000,
    .mask_bits = 24,
    .endpoint_shift = 22,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_COLUMN0_CSR_WINDOW{
    .compare = 0x20'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_COLUMN0_DRAM_WINDOW{
    .compare = 0x23'0000'0000,
    .mask_bits = 30,
    .endpoint_shift = 30,
    .endpoint_size = 0,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

inline constexpr Window HORIZON_X2_DEV1_COLUMN1_L1_WINDOW{
    .compare = 0x21'4000'0000,
    .mask_bits = 24,
    .endpoint_shift = 22,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_COLUMN1_CSR_WINDOW{
    .compare = 0x21'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_COLUMN1_DRAM_WINDOW{
    .compare = 0x23'4000'0000,
    .mask_bits = 30,
    .endpoint_shift = 30,
    .endpoint_size = 0,
    .endpoint_table_offset = 0,
    .translate_address = false,
};

// One per column device. TT_UMD_NOC_ATT=horizon_x2_columns takes their access points in the order
// docs/IP_LAYOUT.md gives, which the build's ip_layout.yaml must follow.
inline constexpr MapData HORIZON_X2_DEV0_COLUMN0_MAP{
    .windows =
        {{HORIZON_X2_DEV0_COLUMN0_L1_WINDOW, HORIZON_X2_DEV0_COLUMN0_DRAM_WINDOW, HORIZON_X2_DEV0_COLUMN0_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_COLUMN0_WORDS, HORIZON_X2_COLUMN0_DRAM_WORDS, HORIZON_X2_COLUMN0_WORDS}},
    .package_offset_x = 0,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x10'4C00'0000,
};

inline constexpr MapData HORIZON_X2_DEV0_COLUMN1_MAP{
    .windows =
        {{HORIZON_X2_DEV0_COLUMN1_L1_WINDOW, HORIZON_X2_DEV0_COLUMN1_DRAM_WINDOW, HORIZON_X2_DEV0_COLUMN1_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_COLUMN1_WORDS, HORIZON_X2_COLUMN1_DRAM_WORDS, HORIZON_X2_COLUMN1_WORDS}},
    .package_offset_x = 1,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x11'4C00'0000,
};

inline constexpr MapData HORIZON_X2_DEV1_COLUMN0_MAP{
    .windows =
        {{HORIZON_X2_DEV1_COLUMN0_L1_WINDOW, HORIZON_X2_DEV1_COLUMN0_DRAM_WINDOW, HORIZON_X2_DEV1_COLUMN0_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_COLUMN0_WORDS, HORIZON_X2_COLUMN0_DRAM_WORDS, HORIZON_X2_COLUMN0_WORDS}},
    .package_offset_x = 0,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x20'4C00'0000,
};

inline constexpr MapData HORIZON_X2_DEV1_COLUMN1_MAP{
    .windows =
        {{HORIZON_X2_DEV1_COLUMN1_L1_WINDOW, HORIZON_X2_DEV1_COLUMN1_DRAM_WINDOW, HORIZON_X2_DEV1_COLUMN1_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_COLUMN1_WORDS, HORIZON_X2_COLUMN1_DRAM_WORDS, HORIZON_X2_COLUMN1_WORDS}},
    .package_offset_x = 1,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x21'4C00'0000,
};

}  // namespace tt::umd::att
