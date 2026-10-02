// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/coordinates/att/att_map.hpp"

namespace tt::umd::att {

/**
 * The Horizon 2x3 host ATT map, whole or split into per-column devices: how the host's flat
 * addresses reach tiles through the NOC2AXI bridge ATTs. The build's att_program.yaml programs it
 * into aether's bridge mask entry 0 (tt-umd-simulators emu/horizon_split/att). The tile ATTs, used
 * by the cores, get tt-metal's horizon_2x3 map instead (TT_METAL_NOC_ATT=horizon_2x3).
 *
 * One window covers every role: the selector is address bits 51:40, and selector 0 is the issuing
 * tile itself, so no core sits there. Selectors 1-6 are the tiles of a 2x3 frame. Coordinates are
 * in the device's frame, as in its soc descriptor, and each device's ATTs map them to its tiles'
 * NODE_IDs: a split device (1x3, x = 0 on either column) uses selectors 1, 3 and 5, and its ATTs
 * park 2, 4 and 6.
 */
inline constexpr Window HORIZON_2X3_WINDOW{
    .compare = 0,
    .mask_bits = 52,
    .endpoint_shift = 40,
    .endpoint_size = 12,
    .endpoint_table_offset = 0,
    .translate_address = true,
};

// Selectors 1, 2: Tensix (0,0), (1,0).
inline constexpr uint16_t HORIZON_2X3_WORKER_ENDPOINT_WORDS[] = {ENDPOINT_UNPOPULATED, 0x000, 0x001};

// Selectors 5, 6: the NOC2AXI tiles, which front DRAM.
inline constexpr uint16_t HORIZON_2X3_DRAM_ENDPOINT_WORDS[] = {
    ENDPOINT_UNPOPULATED,
    ENDPOINT_UNPOPULATED,
    ENDPOINT_UNPOPULATED,
    ENDPOINT_UNPOPULATED,
    ENDPOINT_UNPOPULATED,
    0x080,
    0x081,
};

// Selectors 3, 4: Dispatch (0,1), (1,1); 5, 6: the NOC2AXI tiles' own registers.
inline constexpr uint16_t HORIZON_2X3_FULL_TILE_ENDPOINT_WORDS[] = {
    ENDPOINT_UNPOPULATED,
    ENDPOINT_UNPOPULATED,
    ENDPOINT_UNPOPULATED,
    0x040,
    0x041,
    0x080,
    0x081,
};

inline constexpr MapData HORIZON_2X3_MAP{
    .windows = {{HORIZON_2X3_WINDOW, HORIZON_2X3_WINDOW, HORIZON_2X3_WINDOW}},
    .endpoint_words =
        {{HORIZON_2X3_WORKER_ENDPOINT_WORDS, HORIZON_2X3_DRAM_ENDPOINT_WORDS, HORIZON_2X3_FULL_TILE_ENDPOINT_WORDS}},
    .package_offset_x = 0,
    .package_offset_y = 0,
};

}  // namespace tt::umd::att
