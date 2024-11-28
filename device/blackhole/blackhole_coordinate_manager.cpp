/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/blackhole_coordinate_manager.h"

using namespace tt::umd;

void BlackholeCoordinateManager::translate_tensix_coords() {
    size_t num_harvested_x = __builtin_popcount(tensix_harvesting_mask);
    size_t grid_size_x = tensix_grid_size.x;
    size_t grid_size_y = tensix_grid_size.y;

    size_t logical_x = 0;
    for (size_t x = 0; x < grid_size_x; x++) {
        if (!(tensix_harvesting_mask & (1 << x))) {
            for (size_t y = 0; y < grid_size_y; y++) {
                const tt_xy_pair& tensix_core = tensix_cores[x + y * grid_size_x];
                tensix_logical_to_physical[{logical_x, y}] =
                    CoreCoord(tensix_core.x, tensix_core.y, CoreType::TENSIX, CoordSystem::PHYSICAL);
                tensix_physical_to_logical[tensix_core] =
                    CoreCoord(logical_x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            }
            logical_x++;
        }
    }

    for (size_t x = 0; x < grid_size_x - num_harvested_x; x++) {
        for (size_t y = 0; y < grid_size_y; y++) {
            const tt_xy_pair& tensix_core = tensix_cores[x + y * grid_size_x];
            tensix_logical_to_virtual[{x, y}] =
                CoreCoord(tensix_core.x, tensix_core.y, CoreType::TENSIX, CoordSystem::VIRTUAL);
            tensix_virtual_to_logical[tensix_core] = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
        }
    }

    fill_tensix_logical_to_translated();
}

void BlackholeCoordinateManager::fill_tensix_logical_to_translated() {
    const size_t num_harvested_x = __builtin_popcount(tensix_harvesting_mask);
    const size_t grid_size_x = tensix_grid_size.x;
    const size_t grid_size_y = tensix_grid_size.y;

    for (size_t x = 0; x < grid_size_x - num_harvested_x; x++) {
        for (size_t y = 0; y < grid_size_y; y++) {
            const CoreCoord virtual_coord = tensix_logical_to_virtual[{x, y}];
            const size_t translated_x = virtual_coord.x;
            const size_t translated_y = virtual_coord.y;
            tensix_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);
            tensix_translated_to_logical[{translated_x, translated_y}] =
                CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
        }
    }
}

void BlackholeCoordinateManager::translate_dram_coords() {
    size_t num_harvested_banks = __builtin_popcount(dram_harvesting_mask);

    for (size_t x = 0; x < dram_grid_size.x - num_harvested_banks; x++) {
        for (size_t y = 0; y < dram_grid_size.y; y++) {
            const tt_xy_pair& dram_core = dram_cores[x * dram_grid_size.y + y];
            dram_logical_to_virtual[{x, y}] = CoreCoord(dram_core.x, dram_core.y, CoreType::DRAM, CoordSystem::VIRTUAL);
            dram_virtual_to_logical[dram_core] = CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);
        }
    }

    size_t logical_x = 0;
    for (size_t x = 0; x < dram_grid_size.x; x++) {
        if (!(dram_harvesting_mask & (1 << x))) {
            for (size_t y = 0; y < dram_grid_size.y; y++) {
                const tt_xy_pair& dram_core = dram_cores[x * dram_grid_size.y + y];
                dram_logical_to_physical[{logical_x, y}] =
                    CoreCoord(dram_core.x, dram_core.y, CoreType::DRAM, CoordSystem::PHYSICAL);
                dram_physical_to_logical[dram_core] = CoreCoord(logical_x, y, CoreType::DRAM, CoordSystem::LOGICAL);
            }
            logical_x++;
        }
    }
}

void BlackholeCoordinateManager::fill_eth_logical_to_translated() {
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

void BlackholeCoordinateManager::fill_pcie_logical_to_translated() {
    pcie_logical_to_translated[{0, 0}] = CoreCoord(
        pcie_translated_coordinate_start_x,
        pcie_translated_coordinate_start_y,
        CoreType::PCIE,
        CoordSystem::TRANSLATED);
    pcie_translated_to_logical[{pcie_translated_coordinate_start_x, pcie_translated_coordinate_start_y}] =
        CoreCoord(0, 0, CoreType::PCIE, CoordSystem::LOGICAL);
}
