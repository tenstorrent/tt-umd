// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "coordinates/wormhole_coordinate_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "coordinates/coordinate_manager_utils.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

using namespace coordinate_utils;

namespace {

// Translated x of a core in a Tensix column. The hardware translates columns, so this holds for every core type in
// that column, whichever cores the descriptor lists.
size_t translated_column_x(size_t noc0_x) {
    const auto it = std::find(wormhole::T6_X_LOCATIONS.begin(), wormhole::T6_X_LOCATIONS.end(), noc0_x);
    UMD_ASSERT(
        it != wormhole::T6_X_LOCATIONS.end(),
        error::RuntimeError,
        "NOC0 x " + std::to_string(noc0_x) + " is not a Tensix column.");
    return wormhole::tensix_translated_coordinate_start_x + (it - wormhole::T6_X_LOCATIONS.begin());
}

}  // namespace

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
    return {
        tensix_grid_size.x,
        tensix_grid_size.y - count_harvested(harvesting_masks.tensix_harvesting_mask, tensix_grid_size.y)};
}

void WormholeCoordinateManager::fill_tensix_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_tensix_default_noc0_translated_mapping();
        return;
    }
    // Working rows are numbered first, harvested rows after them.
    const std::vector<size_t> row_order =
        working_then_harvested(tensix_grid_size.y, harvesting_masks.tensix_harvesting_mask);
    for (size_t index = 0; index < row_order.size(); index++) {
        const size_t translated_y = wormhole::tensix_translated_coordinate_start_y + index;
        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            const tt_xy_pair& noc0_core = tensix_cores[row_order[index] * tensix_grid_size.x + x];
            add_core_translation(
                CoreCoord(translated_column_x(noc0_core.x), translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED),
                noc0_core);
        }
    }
}

void WormholeCoordinateManager::fill_eth_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_eth_default_noc0_translated_mapping();
        return;
    }
    // ETH rows are numbered from eth_translated_coordinate_start_y in NOC0 order; columns translate like Tensix ones.
    std::set<size_t> eth_rows;
    for (const tt_xy_pair& eth_core : eth_cores) {
        eth_rows.insert(eth_core.y);
    }
    for (const tt_xy_pair& eth_core : eth_cores) {
        const size_t row_index = std::distance(eth_rows.begin(), eth_rows.find(eth_core.y));
        add_core_translation(
            CoreCoord(
                translated_column_x(eth_core.x),
                wormhole::eth_translated_coordinate_start_y + row_index,
                CoreType::ETH,
                CoordSystem::TRANSLATED),
            eth_core);
    }
}

// DRAM, PCIe and ARC cores keep their NOC0 coordinates in translated space.
void WormholeCoordinateManager::fill_dram_noc0_translated_mapping() { fill_dram_default_noc0_translated_mapping(); }

void WormholeCoordinateManager::fill_pcie_noc0_translated_mapping() { fill_pcie_default_noc0_translated_mapping(); }

void WormholeCoordinateManager::fill_arc_noc0_translated_mapping() { fill_arc_default_noc0_translated_mapping(); }

}  // namespace tt::umd
