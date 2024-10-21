/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <optional>
#include <tuple>
#include <vector>
#include <map>
#include <set>

#include "device/coordinate_manager.h"

class BlackholeCoordinateManager : public CoordinateManager {

public:
    BlackholeCoordinateManager(const tt_xy_pair& worker_grid_size, const std::vector<tt_xy_pair>& workers, std::size_t harvesting_mask)
        : CoordinateManager(worker_grid_size, workers, harvesting_mask) {}

protected: 
    std::set<std::size_t> get_x_coordinates_to_harvest(std::size_t harvesting_mask) override;
};