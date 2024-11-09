/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wormhole_coordinate_manager.h"

std::set<std::size_t> WormholeCoordinateManager::get_y_coordinates_to_harvest(std::size_t harvesting_mask) {
    std::set<std::size_t> y_to_harvest;
    std::size_t logical_y = 0;
    while (harvesting_mask > 0) {
        if (harvesting_mask & 1) {
            y_to_harvest.insert(logical_y);
        }
        logical_y++;
        harvesting_mask >>= 1;
    }
    return y_to_harvest;
}

tt_translated_coords WormholeCoordinateManager::to_translated_coords(tt_logical_coords logical_coords) {
    return tt_translated_coords(logical_coords.x + translated_coordinate_start_x, logical_coords.y + translated_coordinate_start_y);
}

tt_logical_coords WormholeCoordinateManager::to_logical_coords(tt_translated_coords translated_coords) {
    return tt_logical_coords(translated_coords.x - translated_coordinate_start_x, translated_coords.y - translated_coordinate_start_y);
}
