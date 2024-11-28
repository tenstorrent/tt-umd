/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/wormhole_coordinate_manager.h"

using namespace tt::umd;

void WormholeCoordinateManager::fill_tensix_logical_to_translated() {
    size_t num_harvested_y = __builtin_popcount(tensix_harvesting_mask);

    for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            const size_t translated_x = x + tensix_translated_coordinate_start_x;
            const size_t translated_y = y + tensix_translated_coordinate_start_y;
            tensix_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);
            tensix_translated_to_logical[tt_xy_pair(translated_x, translated_y)] =
                CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
        }
    }
}

void WormholeCoordinateManager::fill_eth_logical_to_translated() {
    for (size_t x = 0; x < eth_grid_size.x; x++) {
        for (size_t y = 0; y < eth_grid_size.y; y++) {
            const size_t translated_x = x + eth_translated_coordinate_start_x;
            const size_t translated_y = y + eth_translated_coordinate_start_y;
            eth_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::ETH, CoordSystem::TRANSLATED);
            eth_translated_to_logical[{translated_x, translated_y}] =
                CoreCoord(x, y, CoreType::ETH, CoordSystem::LOGICAL);
        }
    }
}
