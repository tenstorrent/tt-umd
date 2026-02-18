// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/coordinates/wormhole_coordinate_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace tt::umd {

// NOC0 to Translated for DRAM
// clang-format off
static std::unordered_map<tt_xy_pair, tt_xy_pair> dram_coord_map = {
    {{0, 0}, {16, 16}},
    {{0, 1}, {16, 18}},
    {{0, 2}, {16, 19}},
    {{0, 3}, {16, 20}},
    {{0, 4}, {16, 27}},
    {{0, 5}, {16, 21}},
    {{0, 6}, {16, 17}},
    {{0, 7}, {16, 22}},
    {{0, 8}, {16, 23}},
    {{0, 9}, {16, 24}},
    {{0, 10}, {16, 25}},
    {{0, 11}, {16, 26}},
    
    {{5, 0}, {17, 16}},
    {{5, 1}, {17, 18}},
    {{5, 2}, {17, 19}},
    {{5, 3}, {17, 20}},
    {{5, 4}, {17, 27}},
    {{5, 5}, {17, 21}},
    {{5, 6}, {17, 17}},
    {{5, 7}, {17, 22}},
    {{5, 8}, {17, 23}},
    {{5, 9}, {17, 24}},
    {{5, 10}, {17, 25}},
    {{5, 11}, {17, 26}}
};

// clang-format on

WormholeCoordinateManager::WormholeCoordinateManager(
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
    const tt_xy_pair& tensix_grid_size,
    const std::vector<tt_xy_pair>& tensix_cores,
    const tt_xy_pair& dram_grid_size,
    const std::vector<tt_xy_pair>& dram_cores,
    const std::vector<tt_xy_pair>& eth_cores,
    const tt_xy_pair& arc_grid_size,
    const std::vector<tt_xy_pair>& arc_cores,
    const tt_xy_pair& pcie_grid_size,
    const std::vector<tt_xy_pair>& pcie_cores,
    const std::vector<tt_xy_pair>& router_cores,
    const std::vector<tt_xy_pair>& security_cores,
    const std::vector<tt_xy_pair>& l2cpu_cores,
    const std::vector<uint32_t>& noc0_x_to_noc1_x,
    const std::vector<uint32_t>& noc0_y_to_noc1_y) :
    CoordinateManager(
        noc_translation_enabled,
        harvesting_masks,
        tensix_grid_size,
        tensix_cores,
        dram_grid_size,
        dram_cores,
        eth_cores,
        arc_grid_size,
        arc_cores,
        pcie_grid_size,
        pcie_cores,
        router_cores,
        security_cores,
        l2cpu_cores,
        noc0_x_to_noc1_x,
        noc0_y_to_noc1_y) {
    initialize();
}

void WormholeCoordinateManager::fill_tensix_noc0_translated_mapping() {
    size_t num_harvested_y = CoordinateManager::get_num_harvested(harvesting_masks.tensix_harvesting_mask);

    for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            const tt_xy_pair noc0_pair = to_noc0_map[logical_coord];
            const size_t translated_x = x + wormhole::tensix_translated_coordinate_start_x;
            const size_t translated_y = y + wormhole::tensix_translated_coordinate_start_y;

            CoreCoord translated_coord =
                CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, noc0_pair);
        }
    }

    size_t translated_y = wormhole::tensix_translated_coordinate_start_y + tensix_grid_size.y - num_harvested_y;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (harvesting_masks.tensix_harvesting_mask & (1 << y)) {
            for (size_t x = 0; x < tensix_grid_size.x; x++) {
                const tt_xy_pair noc0_core = tensix_cores[y * tensix_grid_size.x + x];
                const size_t translated_x = x + wormhole::tensix_translated_coordinate_start_x;
                CoreCoord translated_coord =
                    CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);

                add_core_translation(translated_coord, noc0_core);
            }
            translated_y++;
        }
    }
}

void WormholeCoordinateManager::fill_dram_noc0_translated_mapping() {
    for (auto dram_core : dram_cores) {
        CoreCoord translated_coord = CoreCoord(dram_core, CoreType::DRAM, CoordSystem::TRANSLATED);
        auto xy_translated_coord = dram_coord_map.at(dram_core);
        translated_coord.x = xy_translated_coord.x;
        translated_coord.y = xy_translated_coord.y;
        add_core_translation(translated_coord, dram_core);
    }
}

void WormholeCoordinateManager::fill_eth_noc0_translated_mapping() {
    for (auto eth_core : eth_cores) {
        CoreCoord translated_coord = CoreCoord(eth_core, CoreType::ETH, CoordSystem::TRANSLATED);

        // X coordinate is in range [1-4], [6-9], but it should be consecutive in translated coordinates.
        if (translated_coord.x > 5) {
            translated_coord.x -= 1;
        }
        // Since the translated coordinates start from 1, we need to subtract 1 to the translated X coordinate.
        translated_coord.x -= 1;
        translated_coord.x += wormhole::eth_translated_coordinate_start_x;

        // Y coordinate is either 0 or 6, but it should be consecutive in translated coordinates.
        if (translated_coord.y == 6) {
            translated_coord.y = 1;
        }
        translated_coord.y += wormhole::eth_translated_coordinate_start_y;

        add_core_translation(translated_coord, eth_core);
    }
}

void WormholeCoordinateManager::fill_pcie_noc0_translated_mapping() {
    // PCIE cores are not translated in Wormhole.
    fill_pcie_default_noc0_translated_mapping();
}

void WormholeCoordinateManager::fill_arc_noc0_translated_mapping() {
    // ARC cores are not translated in Wormhole.
    fill_arc_default_noc0_translated_mapping();
}

tt_xy_pair WormholeCoordinateManager::get_tensix_grid_size() const {
    return {
        tensix_grid_size.x,
        tensix_grid_size.y - CoordinateManager::get_num_harvested(harvesting_masks.tensix_harvesting_mask)};
}

}  // namespace tt::umd
