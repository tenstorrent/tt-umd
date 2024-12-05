/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "quasar_coordinate_manager.h"

std::set<std::size_t> QuasarCoordinateManager::get_x_coordinates_to_harvest(std::size_t harvesting_mask) {
    std::set<std::size_t> x_to_harvest;
    std::size_t logical_x = 0;
    while (harvesting_mask > 0) {
        if (harvesting_mask & 1) {
            x_to_harvest.insert(logical_x);
        }
        logical_x++;
        harvesting_mask >>= 1;
    }
    return x_to_harvest;
}

tt_translated_coords QuasarCoordinateManager::to_translated_coords(tt_logical_coords logical_coords) {
    tt_virtual_coords virtual_coords = to_virtual_coords(logical_coords);
    return tt_translated_coords(virtual_coords.x, virtual_coords.y);
}

tt_logical_coords QuasarCoordinateManager::to_logical_coords(tt_translated_coords translated_coords) {
    tt_virtual_coords virtual_coords = tt_virtual_coords(translated_coords.x, translated_coords.y);
    return CoordinateManager::to_logical_coords(virtual_coords);
}
