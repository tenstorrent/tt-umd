/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <map>
#include <set>
#include <vector>

#include "umd/device/tt_arch_types.h"
#include "umd/device/tt_xy_pair.h"

class CoordinateManager {
public:
    CoordinateManager(
        const tt_xy_pair& worker_grid_size, const std::vector<tt_xy_pair>& workers, std::size_t harvesting_mask) :
        worker_grid_size(worker_grid_size), workers(workers), harvesting_mask(harvesting_mask) {}

    virtual void perform_harvesting(std::size_t harvesting_mask);

    virtual tt_physical_coords to_physical_coords(tt_logical_coords logical_coords);
    virtual tt_translated_coords to_translated_coords(tt_logical_coords logical_coords);
    virtual tt_virtual_coords to_virtual_coords(tt_logical_coords logical_coords);

    virtual tt_logical_coords to_logical_coords(tt_physical_coords physical_coords);
    virtual tt_virtual_coords to_virtual_coords(tt_physical_coords physical_coords);
    virtual tt_translated_coords to_translated_coords(tt_physical_coords physical_coords);

    virtual tt_logical_coords to_logical_coords(tt_virtual_coords virtual_coords);
    virtual tt_physical_coords to_physical_coords(tt_virtual_coords virtual_coords);
    virtual tt_translated_coords to_translated_coords(tt_virtual_coords virtual_coords);

    virtual tt_logical_coords to_logical_coords(tt_translated_coords translated_coords);
    virtual tt_physical_coords to_physical_coords(tt_translated_coords translated_coords);
    virtual tt_virtual_coords to_virtual_coords(tt_translated_coords translated_coords);

    static std::unique_ptr<CoordinateManager> get_coordinate_manager(
        tt::ARCH arch,
        const tt_xy_pair& worker_grid_size,
        const std::vector<tt_xy_pair>& workers,
        std::size_t harvesting_mask);

    CoordinateManager(CoordinateManager& other) = default;

    virtual ~CoordinateManager() {}

protected:
    virtual void clear_harvesting_structures();

    virtual std::set<std::size_t> get_x_coordinates_to_harvest(std::size_t harvesting_mask);
    virtual std::set<std::size_t> get_y_coordinates_to_harvest(std::size_t harvesting_mask);

    virtual void fill_logical_to_physical_mapping(
        const std::set<size_t>& x_to_harvest,
        const std::set<size_t>& y_to_harvest,
        const std::set<size_t>& physical_x_unharvested,
        const std::set<size_t>& physical_y_unharvested);
    virtual void fill_logical_to_virtual_mapping(
        const std::set<size_t>& physical_x_unharvested, const std::set<size_t>& physical_y_unharvested);

    std::map<std::size_t, std::size_t> physical_y_to_logical_y;
    std::map<std::size_t, std::size_t> physical_x_to_logical_x;

    std::vector<std::size_t> logical_y_to_physical_y;
    std::vector<std::size_t> logical_x_to_physical_x;

    std::vector<std::size_t> logical_y_to_virtual_y;
    std::vector<std::size_t> logical_x_to_virtual_x;

    std::map<std::size_t, std::size_t> virtual_y_to_logical_y;
    std::map<std::size_t, std::size_t> virtual_x_to_logical_x;

    const tt_xy_pair worker_grid_size;
    const std::vector<tt_xy_pair>& workers;
    const std::size_t harvesting_mask;
};
