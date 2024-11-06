/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "device/coordinate_manager.h"
#include <memory>
#include "coordinate_manager.h"
#include "grayskull/grayskull_coordinate_manager.h"

tt_physical_coords CoordinateManager::to_physical_coords(tt_logical_coords logical_coords) {
    return tt_physical_coords(logical_x_to_physical_x[logical_coords.x], logical_y_to_physical_y[logical_coords.y]);
}

// TODO(pjanevski): this is different for Wormhole and Blackhole.
// investigate and implement
tt_translated_coords CoordinateManager::to_translated_coords(tt_logical_coords logical_coords) {
    tt_physical_coords physical_coords = to_physical_coords(logical_coords);
    return tt_translated_coords(physical_coords.x, physical_coords.y);
}

tt_virtual_coords CoordinateManager::to_virtual_coords(tt_logical_coords logical_coords) {
    return tt_virtual_coords(logical_x_to_virtual_x[logical_coords.x], logical_y_to_virtual_y[logical_coords.y]);
}

tt_logical_coords CoordinateManager::to_logical_coords(tt_physical_coords physical_coords) {
    return tt_logical_coords(physical_x_to_logical_x[physical_coords.x], physical_y_to_logical_y[physical_coords.y]);
}

tt_virtual_coords CoordinateManager::to_virtual_coords(tt_physical_coords physical_coords) {
    return to_virtual_coords(to_logical_coords(physical_coords));
}

tt_translated_coords CoordinateManager::to_translated_coords(tt_physical_coords physical_coords) {
    return to_translated_coords(to_logical_coords(physical_coords));
}

tt_logical_coords CoordinateManager::to_logical_coords(tt_virtual_coords virtual_coords) {
    return tt_logical_coords(virtual_x_to_logical_x[virtual_coords.x], virtual_y_to_logical_y[virtual_coords.y]);
}

tt_physical_coords CoordinateManager::to_physical_coords(tt_virtual_coords virtual_coords) {
    return to_physical_coords(to_logical_coords(virtual_coords));
}

tt_translated_coords CoordinateManager::to_translated_coords(tt_virtual_coords virtual_coords) {
    return to_translated_coords(to_logical_coords(virtual_coords));
}

tt_logical_coords CoordinateManager::to_logical_coords(tt_translated_coords translated_coords) {
    tt_physical_coords physical_coords = tt_physical_coords(translated_coords.x, translated_coords.y);
    return to_logical_coords(physical_coords);
}

tt_physical_coords CoordinateManager::to_physical_coords(tt_translated_coords translated_coords) {
    return to_physical_coords(to_logical_coords(translated_coords));
}

tt_virtual_coords CoordinateManager::to_virtual_coords(tt_translated_coords translated_coords) {
    return to_virtual_coords(to_logical_coords(translated_coords));
}

void CoordinateManager::clear_harvesting_structures() {
    logical_x_to_physical_x.clear();
    logical_y_to_physical_y.clear();
    logical_x_to_virtual_x.clear();
    logical_y_to_virtual_y.clear();
    physical_x_to_logical_x.clear();
    physical_y_to_logical_y.clear();
    virtual_x_to_logical_x.clear();
    virtual_y_to_logical_y.clear();
}

std::set<std::size_t> CoordinateManager::get_x_coordinates_to_harvest(std::size_t harvesting_mask) {
    return {};
}

std::set<std::size_t> CoordinateManager::get_y_coordinates_to_harvest(std::size_t harvesting_mask) {
    return {};
}

void CoordinateManager::perform_harvesting(std::size_t harvesting_mask) {
    clear_harvesting_structures();

    std::set<size_t> physical_x_unharvested;
    std::set<size_t> physical_y_unharvested;
    for (auto core : workers) {
        physical_x_unharvested.insert(core.x);
        physical_y_unharvested.insert(core.y);
    }

    std::set<std::size_t> x_coordinates_to_harvest = get_x_coordinates_to_harvest(harvesting_mask);
    std::set<std::size_t> y_coordinates_to_harvest = get_y_coordinates_to_harvest(harvesting_mask);

    std::size_t num_harvested_y = y_coordinates_to_harvest.size();
    std::size_t num_harvested_x = x_coordinates_to_harvest.size();

    std::size_t grid_size_x = worker_grid_size.x;
    std::size_t grid_size_y = worker_grid_size.y;

    logical_x_to_physical_x.resize(grid_size_x - num_harvested_x);
    logical_y_to_physical_y.resize(grid_size_y - num_harvested_y);

    logical_x_to_virtual_x.resize(grid_size_x - num_harvested_x);
    logical_y_to_virtual_y.resize(grid_size_y - num_harvested_y);

    fill_logical_to_physical_mapping(x_coordinates_to_harvest, y_coordinates_to_harvest, physical_x_unharvested, physical_y_unharvested);
    fill_logical_to_virtual_mapping(physical_x_unharvested, physical_y_unharvested);
}

void CoordinateManager::fill_logical_to_physical_mapping(
    const std::set<size_t>& x_to_harvest, const std::set<size_t>& y_to_harvest,
    const std::set<size_t>& physical_x_unharvested, const std::set<size_t>& physical_y_unharvested) {
    
    auto physical_y_it = physical_y_unharvested.begin();
    std::size_t logical_y = 0;
    for (size_t y = 0; y < worker_grid_size.y; y++) {
        if (y_to_harvest.find(y) == y_to_harvest.end()) {
            logical_y_to_physical_y[logical_y] = *physical_y_it;
            if (physical_y_to_logical_y.find(*physical_y_it) != physical_y_to_logical_y.end()) {
                throw std::runtime_error("Duplicate physical y coordinate found in the worker cores");
            }
            physical_y_to_logical_y[*physical_y_it] = logical_y;
            logical_y++;
            physical_y_it++;
        } else {
            physical_y_it++;
        }
    }

    auto physical_x_it = physical_x_unharvested.begin();
    std::size_t logical_x = 0;
    for(std::size_t x = 0; x < worker_grid_size.x; x++) {
        if (x_to_harvest.find(x) == x_to_harvest.end()) {
            logical_x_to_physical_x[logical_x] = *physical_x_it;
            if (physical_x_to_logical_x.find(*physical_x_it) != physical_x_to_logical_x.end()) {
                throw std::runtime_error("Duplicate physical x coordinate found in the worker cores");
            }
            physical_x_to_logical_x[*physical_x_it] = logical_x;
            logical_x++;
            physical_x_it++;
        } else {
            physical_x_it++;
        }
    }
}

void CoordinateManager::fill_logical_to_virtual_mapping(const std::set<size_t>& physical_x_unharvested, const std::set<size_t>& physical_y_unharvested) {
    auto physical_y_it = physical_y_unharvested.begin();
    for (std::size_t y = 0; y < logical_y_to_virtual_y.size(); y++) {
        logical_y_to_virtual_y[y] = *physical_y_it;
        if (virtual_y_to_logical_y.find(*physical_y_it) != virtual_y_to_logical_y.end()) {
            throw std::runtime_error("Duplicate virtual y coordinate found in the worker cores");
        }
        virtual_y_to_logical_y[*physical_y_it] = y;
        physical_y_it++;
    }

    auto physical_x_it = physical_x_unharvested.begin();
    for (std::size_t x = 0; x < logical_x_to_virtual_x.size(); x++) {
        logical_x_to_virtual_x[x] = *physical_x_it;
        if (virtual_x_to_logical_x.find(*physical_x_it) != virtual_x_to_logical_x.end()) {
            throw std::runtime_error("Duplicate virtual x coordinate found in the worker cores");
        }
        virtual_x_to_logical_x[*physical_x_it] = x;
        physical_x_it++;
    }
}

#include "device/blackhole/blackhole_coordinate_manager.h"
#include "device/grayskull/grayskull_coordinate_manager.h"
#include "device/wormhole/wormhole_coordinate_manager.h"

std::unique_ptr<CoordinateManager> CoordinateManager::get_coordinate_manager(
    tt::ARCH arch,
    const tt_xy_pair& worker_grid_size,
    const std::vector<tt_xy_pair>& workers,
    std::size_t harvesting_mask) {

    switch (arch) {
        case tt::ARCH::GRAYSKULL:
            return std::make_unique<GrayskullCoordinateManager>(worker_grid_size, workers, harvesting_mask);
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeCoordinateManager>(worker_grid_size, workers, harvesting_mask);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeCoordinateManager>(worker_grid_size, workers, harvesting_mask);
        case tt::ARCH::Invalid:
            throw std::runtime_error("Invalid architecture for creating coordinate manager");
    }

    throw std::runtime_error("Invalid architecture for creating coordinate manager");
}
