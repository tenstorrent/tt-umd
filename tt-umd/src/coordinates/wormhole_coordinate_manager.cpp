// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "coordinates/wormhole_coordinate_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <vector>

#include "tt-umd/arch/wormhole_implementation.hpp"

namespace tt::umd {

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
    const std::vector<tt_xy_pair>& dispatch_cores,
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
        dispatch_cores,
        noc0_x_to_noc1_x,
        noc0_y_to_noc1_y) {}

tt_xy_pair WormholeCoordinateManager::get_tensix_grid_size() const {
    return {tensix_grid_size.x, tensix_grid_size.y - get_num_harvested(harvesting_masks.tensix_harvesting_mask)};
}

namespace {

// Translated X of the Tensix/ETH column at the given NOC0 X: the columns are numbered in NOC0 order
// from the translated start, skipping the DRAM column.
size_t translated_x(size_t noc0_x) {
    const auto& columns = wormhole::T6_X_LOCATIONS;
    const auto it = std::find(columns.begin(), columns.end(), noc0_x);
    return wormhole::tensix_translated_coordinate_start_x + std::distance(columns.begin(), it);
}

}  // namespace

void WormholeCoordinateManager::fill_tensix_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_tensix_default_noc0_translated_mapping();
        return;
    }

    // The firmware numbers the Tensix rows that aren't harvested first, in NOC0 order, followed by
    // the harvested ones.
    std::vector<size_t> rows;
    for (const tt_xy_pair& core : tensix_cores) {
        rows.push_back(core.y);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    std::vector<size_t> row_order;
    for (size_t row = 0; row < rows.size(); row++) {
        if (!(harvesting_masks.tensix_harvesting_mask & (size_t{1} << row))) {
            row_order.push_back(rows[row]);
        }
    }
    for (size_t row = 0; row < rows.size(); row++) {
        if (harvesting_masks.tensix_harvesting_mask & (size_t{1} << row)) {
            row_order.push_back(rows[row]);
        }
    }

    std::map<size_t, size_t> noc0_y_to_translated_y;
    for (size_t position = 0; position < row_order.size(); position++) {
        noc0_y_to_translated_y[row_order[position]] = wormhole::tensix_translated_coordinate_start_y + position;
    }

    for (const tt_xy_pair& core : tensix_cores) {
        add_core_translation(
            CoreCoord(
                translated_x(core.x), noc0_y_to_translated_y.at(core.y), CoreType::TENSIX, CoordSystem::TRANSLATED),
            core);
    }
}

void WormholeCoordinateManager::fill_eth_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_eth_default_noc0_translated_mapping();
        return;
    }

    // The two ETH rows, NOC0 rows 0 and 6, translate to the two rows right above the Tensix block.
    for (const tt_xy_pair& core : eth_cores) {
        const size_t translated_y = wormhole::eth_translated_coordinate_start_y + (core.y == 0 ? 0 : 1);
        add_core_translation(
            CoreCoord(translated_x(core.x), translated_y, CoreType::ETH, CoordSystem::TRANSLATED), core);
    }
}

// The hardware leaves coordinates below 16 untranslated, so DRAM, PCIe and ARC cores are reached at
// their NOC0 coordinates whether translation is on or not.
void WormholeCoordinateManager::fill_dram_noc0_translated_mapping() { fill_dram_default_noc0_translated_mapping(); }

void WormholeCoordinateManager::fill_pcie_noc0_translated_mapping() { fill_pcie_default_noc0_translated_mapping(); }

void WormholeCoordinateManager::fill_arc_noc0_translated_mapping() { fill_arc_default_noc0_translated_mapping(); }

}  // namespace tt::umd
