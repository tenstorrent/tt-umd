/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "device/coordinate_manager.h"

class WormholeCoordinateManager : public CoordinateManager {
public:
    WormholeCoordinateManager(
        const tt_xy_pair& worker_grid_size, const std::vector<tt_xy_pair>& workers, std::size_t harvesting_mask) :
        CoordinateManager(worker_grid_size, workers, harvesting_mask) {}

    tt_translated_coords to_translated_coords(tt_logical_coords logical_coords) override;

    tt_logical_coords to_logical_coords(tt_translated_coords translated_coords) override;

protected:
    std::set<std::size_t> get_y_coordinates_to_harvest(std::size_t harvesting_mask) override;

private:
    static const std::size_t translated_coordinate_start_x = 18;
    static const std::size_t translated_coordinate_start_y = 18;
};
