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
    {{0, 0}, {16, 16}}, // aligned with ethernet tiles - can't be harvested on Wormhole
    {{0, 1}, {16, 18}},
    {{0, 2}, {16, 19}},
    {{0, 3}, {16, 20}},
    {{0, 4}, {16, 27}},
    {{0, 5}, {16, 21}},
    {{0, 6}, {16, 17}}, // aligned with ethernet tiles - can't be harvested on Wormhole
    {{0, 7}, {16, 22}},
    {{0, 8}, {16, 23}},
    {{0, 9}, {16, 24}},
    {{0, 10}, {16, 25}},
    {{0, 11}, {16, 26}},
    
    {{5, 0}, {17, 16}}, // aligned with ethernet tiles - can't be harvested on Wormhole
    {{5, 1}, {17, 18}},
    {{5, 2}, {17, 19}},
    {{5, 3}, {17, 20}},
    {{5, 4}, {17, 27}},
    {{5, 5}, {17, 21}},
    {{5, 6}, {17, 17}}, // aligned with ethernet tiles - can't be harvested on Wormhole
    {{5, 7}, {17, 22}},
    {{5, 8}, {17, 23}},
    {{5, 9}, {17, 24}},
    {{5, 10}, {17, 25}},
    {{5, 11}, {17, 26}}
};

static std::unordered_map<tt_xy_pair, tt_xy_pair> arc_coord_map = {
    {{0, 10}, {16, 25}}
};

static std::unordered_map<tt_xy_pair, tt_xy_pair> pcie_coord_map = {
    {{0, 3}, {16, 20}}
};

// clang-format on

// Helper function to reorder DRAM y indices based on harvesting mask.
// DRAM channels must correspond to tensix rows (skipping rows 0 and 6 which are ethernet).
// When a tensix row is harvested, the corresponding DRAM channel moves to the back.
// Input: dram_noc_y values (1,2,3,4,5,7,8,9,10,11 - skipping 0 and 6 which are ethernet-aligned)
// Output: reordered values based on harvesting mask.
static std::vector<size_t> reorder_dram_channels_for_harvesting(uint32_t harvesting_mask) {
    // DRAM NOC y-coordinates that can be harvested (skipping 0 and 6 which are ethernet-aligned).
    std::vector<size_t> dram_channels = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};

    std::vector<size_t> unharvested_channels;
    std::vector<size_t> harvested_channels;

    // Map each DRAM channel to its corresponding tensix row index in the mask
    // DRAM y=1 -> tensix row 0, y=2 -> row 1, y=3 -> row 2, y=4 -> row 3, y=5 -> row 4
    // DRAM y=7 -> tensix row 5, y=8 -> row 6, y=9 -> row 7, y=10 -> row 8, y=11 -> row 9.
    auto dram_y_to_tensix_row = [](size_t dram_y) -> size_t {
        if (dram_y < 6) {
            return dram_y - 1;  // y=1->0, y=2->1, y=3->2, y=4->3, y=5->4
        } else {
            return dram_y - 2;  // y=7->5, y=8->6, y=9->7, y=10->8, y=11->9
        }
    };

    for (size_t channel : dram_channels) {
        size_t tensix_row = dram_y_to_tensix_row(channel);
        if (harvesting_mask & (1 << tensix_row)) {
            harvested_channels.push_back(channel);
        } else {
            unharvested_channels.push_back(channel);
        }
    }

    // Concatenate: unharvested first, then harvested.
    std::vector<size_t> result = unharvested_channels;
    result.insert(result.end(), harvested_channels.begin(), harvested_channels.end());

    return result;
}

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
    // Get the reordered DRAM channels based on harvesting.
    std::vector<size_t> reordered_channels =
        reorder_dram_channels_for_harvesting(harvesting_masks.tensix_harvesting_mask);

    // Build mapping from original NOC y to reordered translated y.
    std::unordered_map<size_t, size_t> noc_y_to_translated_y;

    // The reordered channels map to consecutive translated y coordinates starting at 18
    // (skipping 16 and 17 which are ethernet-aligned DRAM at y=0 and y=6)
    for (size_t i = 0; i < reordered_channels.size(); i++) {
        size_t original_noc_y = reordered_channels[i];
        size_t translated_y = wormhole::tensix_translated_coordinate_start_y + i;
        noc_y_to_translated_y[original_noc_y] = translated_y;
    }

    // Now translate DRAM coordinates.
    for (auto dram_core : dram_cores) {
        CoreCoord translated_coord = CoreCoord(dram_core, CoreType::DRAM, CoordSystem::TRANSLATED);
        auto xy_translated_coord = dram_coord_map.at(dram_core);
        translated_coord.x = xy_translated_coord.x;

        // Check if this DRAM y-coordinate is one that can be reordered (not ethernet-aligned).
        if (noc_y_to_translated_y.find(dram_core.y) != noc_y_to_translated_y.end()) {
            // Use the reordered y-coordinate.
            translated_coord.y = noc_y_to_translated_y[dram_core.y];
        } else {
            // Ethernet-aligned DRAM (y=0 or y=6) stays at fixed positions (16 or 17).
            translated_coord.y = xy_translated_coord.y;
        }
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
    for (auto pcie_core : pcie_cores) {
        CoreCoord translated_coord = CoreCoord(pcie_core, CoreType::PCIE, CoordSystem::TRANSLATED);
        auto xy_translated_coord = pcie_coord_map.at(pcie_core);
        translated_coord.x = xy_translated_coord.x;
        translated_coord.y = xy_translated_coord.y;
        add_core_translation(translated_coord, pcie_core);
    }
}

void WormholeCoordinateManager::fill_arc_noc0_translated_mapping() {
    for (auto arc_core : arc_cores) {
        CoreCoord translated_coord = CoreCoord(arc_core, CoreType::ARC, CoordSystem::TRANSLATED);
        auto xy_translated_coord = arc_coord_map.at(arc_core);
        translated_coord.x = xy_translated_coord.x;
        translated_coord.y = xy_translated_coord.y;
        add_core_translation(translated_coord, arc_core);
    }
}

tt_xy_pair WormholeCoordinateManager::get_tensix_grid_size() const {
    return {
        tensix_grid_size.x,
        tensix_grid_size.y - CoordinateManager::get_num_harvested(harvesting_masks.tensix_harvesting_mask)};
}

}  // namespace tt::umd
