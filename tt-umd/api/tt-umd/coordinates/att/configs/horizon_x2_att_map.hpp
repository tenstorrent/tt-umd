// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "tt-umd/coordinates/att/att_map.hpp"

namespace tt::umd::att {

/**
 * Two whole Horizon 2x3 devices behind linked crossbars (aether horizon_x2), with each device's
 * NOC2AXI tiles reaching the whole device, each in its own crossbar range: device d's NOC2AXI n
 * fronts ((d + 1) << 36) + (n << 32). Transcribed from tests/att_maps/horizon_x2_per_noc2axi.yaml,
 * the tables the testbench programs into every NOC2AXI and tile of a device (AETHER_ATT_CONFIG), and
 * checked against it.
 *
 * Endpoint words are (y << 6) | x in the device's frame, the whole 2x3 grid: Tensix (0,0), (1,0);
 * Dispatch (0,1), (1,1); NOC2AXI (0,2), (1,2), which front DRAM banks 0 and 1. Words are per
 * selector, in the device table's row order (table_offset + selector), which differs by device.
 */

// L1 unicast, 4 MB per selector, a tile's whole L1: Dispatch (0,1), (1,1), Tensix (0,0), (1,0), then
// the table's remaining rows.
inline constexpr uint16_t HORIZON_X2_DEV0_L1_WORDS[] = {0x040, 0x041, 0x000, 0x001, 0x081, 0x080};
inline constexpr uint16_t HORIZON_X2_DEV1_L1_WORDS[] = {0x040, 0x041, 0x000, 0x001, 0x081};

// Tensix CSR, 128 MB per selector: each tile's whole map, L1 included.
inline constexpr uint16_t HORIZON_X2_CSR_WORDS[] = {0x040, 0x041, 0x000, 0x001};

// DRAM, 1 GB per bank, behind NOC2AXI (0,2) and (1,2), reached straight through the crossbar. The
// tables hold the two banks as two pass-through windows at the crossbar's dram_0 and dram_1 ranges;
// one 2 GB window with a one-bit selector gives the same addresses.
inline constexpr uint16_t HORIZON_X2_DRAM_WORDS[] = {0x080, 0x081};

inline constexpr Window HORIZON_X2_DEV0_NOC2AXI0_L1_WINDOW{
    .compare = 0x10'4000'0000,
    .mask_bits = 25,
    .endpoint_shift = 22,
    .endpoint_size = 3,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_NOC2AXI0_CSR_WINDOW{
    .compare = 0x10'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_NOC2AXI1_L1_WINDOW{
    .compare = 0x11'4000'0000,
    .mask_bits = 25,
    .endpoint_shift = 22,
    .endpoint_size = 3,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_NOC2AXI1_CSR_WINDOW{
    .compare = 0x11'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 1,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV0_DRAM_WINDOW{
    .compare = 0x13'0000'0000,
    .mask_bits = 31,
    .endpoint_shift = 30,
    .endpoint_size = 1,
    .endpoint_table_offset = 6,
    .translate_address = false,
};

inline constexpr Window HORIZON_X2_DEV1_NOC2AXI0_L1_WINDOW{
    .compare = 0x20'4000'0000,
    .mask_bits = 25,
    .endpoint_shift = 22,
    .endpoint_size = 3,
    .endpoint_table_offset = 2,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_NOC2AXI0_CSR_WINDOW{
    .compare = 0x20'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 2,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_NOC2AXI1_L1_WINDOW{
    .compare = 0x21'4000'0000,
    .mask_bits = 25,
    .endpoint_shift = 22,
    .endpoint_size = 3,
    .endpoint_table_offset = 2,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_NOC2AXI1_CSR_WINDOW{
    .compare = 0x21'8000'0000,
    .mask_bits = 29,
    .endpoint_shift = 27,
    .endpoint_size = 2,
    .endpoint_table_offset = 2,
    .translate_address = true,
};

inline constexpr Window HORIZON_X2_DEV1_DRAM_WINDOW{
    .compare = 0x23'0000'0000,
    .mask_bits = 31,
    .endpoint_shift = 30,
    .endpoint_size = 1,
    .endpoint_table_offset = 1,
    .translate_address = false,
};

// One per access point of horizon_x2's ip_layout.yaml, in its order: each NOC2AXI of device 0, then of
// device 1.
inline constexpr MapData HORIZON_X2_DEV0_NOC2AXI0_MAP{
    .windows = {{HORIZON_X2_DEV0_NOC2AXI0_L1_WINDOW, HORIZON_X2_DEV0_DRAM_WINDOW, HORIZON_X2_DEV0_NOC2AXI0_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_DEV0_L1_WORDS, HORIZON_X2_DRAM_WORDS, HORIZON_X2_CSR_WORDS}},
    .package_offset_x = 0,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x10'4C00'0000,
};

inline constexpr MapData HORIZON_X2_DEV0_NOC2AXI1_MAP{
    .windows = {{HORIZON_X2_DEV0_NOC2AXI1_L1_WINDOW, HORIZON_X2_DEV0_DRAM_WINDOW, HORIZON_X2_DEV0_NOC2AXI1_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_DEV0_L1_WORDS, HORIZON_X2_DRAM_WORDS, HORIZON_X2_CSR_WORDS}},
    .package_offset_x = 0,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x11'4C00'0000,
};

inline constexpr MapData HORIZON_X2_DEV1_NOC2AXI0_MAP{
    .windows = {{HORIZON_X2_DEV1_NOC2AXI0_L1_WINDOW, HORIZON_X2_DEV1_DRAM_WINDOW, HORIZON_X2_DEV1_NOC2AXI0_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_DEV1_L1_WORDS, HORIZON_X2_DRAM_WORDS, HORIZON_X2_CSR_WORDS}},
    .package_offset_x = 0,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x20'4C00'0000,
};

inline constexpr MapData HORIZON_X2_DEV1_NOC2AXI1_MAP{
    .windows = {{HORIZON_X2_DEV1_NOC2AXI1_L1_WINDOW, HORIZON_X2_DEV1_DRAM_WINDOW, HORIZON_X2_DEV1_NOC2AXI1_CSR_WINDOW}},
    .endpoint_words = {{HORIZON_X2_DEV1_L1_WORDS, HORIZON_X2_DRAM_WORDS, HORIZON_X2_CSR_WORDS}},
    .package_offset_x = 0,
    .package_offset_y = 0,
    .worker_atomic_compare = 0x21'4C00'0000,
};

}  // namespace tt::umd::att
