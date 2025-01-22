/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/wormhole_coordinate_manager.h"

using namespace tt::umd;

WormholeCoordinateManager::WormholeCoordinateManager(
    const bool noc_translation_enabled,
    const tt_xy_pair& tensix_grid_size,
    const std::vector<tt_xy_pair>& tensix_cores,
    const size_t tensix_harvesting_mask,
    const tt_xy_pair& dram_grid_size,
    const std::vector<tt_xy_pair>& dram_cores,
    const size_t dram_harvesting_mask,
    const std::vector<tt_xy_pair>& eth_cores,
    const size_t eth_harvesting_mask,
    const tt_xy_pair& arc_grid_size,
    const std::vector<tt_xy_pair>& arc_cores,
    const tt_xy_pair& pcie_grid_size,
    const std::vector<tt_xy_pair>& pcie_cores,
    const std::vector<tt_xy_pair>& router_cores) :
    CoordinateManager(
        noc_translation_enabled,
        tensix_grid_size,
        tensix_cores,
        tensix_harvesting_mask,
        dram_grid_size,
        dram_cores,
        dram_harvesting_mask,
        eth_cores,
        eth_harvesting_mask,
        arc_grid_size,
        arc_cores,
        pcie_grid_size,
        pcie_cores,
        router_cores) {
    initialize();
}

void WormholeCoordinateManager::fill_tensix_physical_translated_mapping() {
    size_t num_harvested_y = CoordinateManager::get_num_harvested(tensix_harvesting_mask);

    for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            const tt_xy_pair physical_pair = to_physical_map[logical_coord];
            const size_t translated_x = x + wormhole::tensix_translated_coordinate_start_x;
            const size_t translated_y = y + wormhole::tensix_translated_coordinate_start_y;

            CoreCoord translated_coord =
                CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, physical_pair);
        }
    }

    size_t harvested_index = (tensix_grid_size.y - num_harvested_y) * tensix_grid_size.x;
    size_t translated_y = wormhole::tensix_translated_coordinate_start_y + tensix_grid_size.y - num_harvested_y;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (tensix_harvesting_mask & (1 << y)) {
            for (size_t x = 0; x < tensix_grid_size.x; x++) {
                const tt_xy_pair physical_core = tensix_cores[y * tensix_grid_size.x + x];
                const size_t translated_x = x + wormhole::tensix_translated_coordinate_start_x;
                CoreCoord translated_coord =
                    CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);

                add_core_translation(translated_coord, physical_core);
            }
            translated_y++;
        }
    }
}

void WormholeCoordinateManager::fill_dram_physical_translated_mapping() {
    // DRAM cores are not translated in Wormhole.
    fill_dram_default_physical_translated_mapping();
}

void WormholeCoordinateManager::fill_eth_physical_translated_mapping() {
    const size_t eth_grid_size_x = (num_eth_channels + 1) / 2;
    const size_t eth_grid_size_y = num_eth_channels / eth_grid_size_x;
    for (size_t x = 0; x < eth_grid_size_x; x++) {
        for (size_t y = 0; y < eth_grid_size_y; y++) {
            const size_t translated_x = x + wormhole::eth_translated_coordinate_start_x;
            const size_t translated_y = y + wormhole::eth_translated_coordinate_start_y;
            CoreCoord logical_coord = CoreCoord(0, y * eth_grid_size_x + x, CoreType::ETH, CoordSystem::LOGICAL);
            const tt_xy_pair physical_pair = to_physical_map[logical_coord];
            CoreCoord translated_coord = CoreCoord(translated_x, translated_y, CoreType::ETH, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, physical_pair);
        }
    }
}

void WormholeCoordinateManager::fill_pcie_physical_translated_mapping() {
    // PCIE cores are not translated in Wormhole.
    fill_pcie_default_physical_translated_mapping();
}

void WormholeCoordinateManager::fill_arc_physical_translated_mapping() {
    // ARC cores are not translated in Wormhole.
    fill_arc_default_physical_translated_mapping();
}
