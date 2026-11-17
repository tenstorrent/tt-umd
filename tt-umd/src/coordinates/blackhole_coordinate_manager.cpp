// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "coordinates/blackhole_coordinate_manager.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "coordinates/coordinate_manager_utils.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

using namespace coordinate_utils;

BlackholeCoordinateManager::BlackholeCoordinateManager(
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

void BlackholeCoordinateManager::assert_coordinate_manager_constructor() {
    CoordinateManager::assert_coordinate_manager_constructor();

    // Blackhole chips come with exactly two ETH channels harvested, or all of them on boards without ethernet.
    if (eth_cores.size() == blackhole::NUM_ETH_CHANNELS) {
        const size_t num_harvested_eth = get_num_harvested(harvesting_masks.eth_harvesting_mask);
        UMD_ASSERT(
            num_harvested_eth == 2 || num_harvested_eth == blackhole::NUM_ETH_CHANNELS,
            error::RuntimeError,
            fmt::format(
                "Blackhole has 2 or all {} ETH channels harvested, but ETH harvesting mask {:#x} harvests {}.",
                blackhole::NUM_ETH_CHANNELS,
                harvesting_masks.eth_harvesting_mask,
                num_harvested_eth));
    }
}

void BlackholeCoordinateManager::translate_tensix_coords() {
    // Harvesting removes whole columns.
    UMD_ASSERT(
        get_num_harvested(harvesting_masks.tensix_harvesting_mask) <= tensix_grid_size.x,
        error::RuntimeError,
        "Tensix harvesting mask names more columns than the grid has.");
    size_t logical_x = 0;
    for (size_t x = 0; x < tensix_grid_size.x; x++) {
        if (is_harvested(harvesting_masks.tensix_harvesting_mask, x)) {
            continue;
        }
        for (size_t y = 0; y < tensix_grid_size.y; y++) {
            add_core_translation(
                CoreCoord(logical_x, y, CoreType::TENSIX, CoordSystem::LOGICAL),
                tensix_cores[y * tensix_grid_size.x + x]);
        }
        logical_x++;
    }
    fill_tensix_noc0_translated_mapping();
}

std::vector<CoreCoord> BlackholeCoordinateManager::get_tensix_cores() const {
    return filter_grid_cores_by_mask(
        tensix_cores, CoreType::TENSIX, harvesting_masks.tensix_harvesting_mask, false, [this](size_t index) {
            return index % tensix_grid_size.x;
        });
}

std::vector<CoreCoord> BlackholeCoordinateManager::get_harvested_tensix_cores() const {
    return filter_grid_cores_by_mask(
        tensix_cores, CoreType::TENSIX, harvesting_masks.tensix_harvesting_mask, true, [this](size_t index) {
            return index % tensix_grid_size.x;
        });
}

tt_xy_pair BlackholeCoordinateManager::get_tensix_grid_size() const {
    return {
        tensix_grid_size.x - count_harvested(harvesting_masks.tensix_harvesting_mask, tensix_grid_size.x),
        tensix_grid_size.y};
}

tt_xy_pair BlackholeCoordinateManager::get_harvested_tensix_grid_size() const {
    const size_t num_harvested_columns = count_harvested(harvesting_masks.tensix_harvesting_mask, tensix_grid_size.x);
    return num_harvested_columns == 0 ? tt_xy_pair(0, 0) : tt_xy_pair(num_harvested_columns, tensix_grid_size.y);
}

void BlackholeCoordinateManager::fill_tensix_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_tensix_default_noc0_translated_mapping();
        return;
    }
    // Translated coordinates reuse the NOC0 column positions: working columns take the leftmost ones in order,
    // harvested columns fill the rightmost ones from the right edge inwards. Rows keep their NOC0 y.
    std::vector<size_t> column_order =
        filter_indices_by_mask(tensix_grid_size.x, harvesting_masks.tensix_harvesting_mask, false);
    const std::vector<size_t> harvested_columns =
        filter_indices_by_mask(tensix_grid_size.x, harvesting_masks.tensix_harvesting_mask, true);
    column_order.insert(column_order.end(), harvested_columns.rbegin(), harvested_columns.rend());
    for (size_t translated_column = 0; translated_column < column_order.size(); translated_column++) {
        const size_t translated_x = tensix_cores[translated_column].x;
        for (size_t y = 0; y < tensix_grid_size.y; y++) {
            const tt_xy_pair& noc0_core = tensix_cores[y * tensix_grid_size.x + column_order[translated_column]];
            add_core_translation(
                CoreCoord(translated_x, noc0_core.y, CoreType::TENSIX, CoordSystem::TRANSLATED), noc0_core);
        }
    }
}

void BlackholeCoordinateManager::fill_eth_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_eth_default_noc0_translated_mapping();
        return;
    }
    for (const auto& [translated_coord, noc0_pair] : working_row_translations(
             eth_cores,
             CoreType::ETH,
             harvesting_masks.eth_harvesting_mask,
             blackhole::eth_translated_coordinate_start_x,
             blackhole::eth_translated_coordinate_start_y)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void BlackholeCoordinateManager::fill_dram_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_dram_default_noc0_translated_mapping();
        return;
    }
    // Banks fill two translated columns top to bottom, half of them each, the ports of a bank on consecutive rows.
    // Working banks come first, so a harvested bank ends up at the bottom of the second column.
    const size_t banks_per_column = (dram_grid_size.x + 1) / 2;
    const std::vector<size_t> bank_order =
        working_then_harvested(dram_grid_size.x, harvesting_masks.dram_harvesting_mask);
    for (size_t index = 0; index < bank_order.size(); index++) {
        for (size_t port = 0; port < dram_grid_size.y; port++) {
            const size_t translated_x = blackhole::dram_translated_coordinate_start_x + index / banks_per_column;
            const size_t translated_y =
                blackhole::dram_translated_coordinate_start_y + (index % banks_per_column) * dram_grid_size.y + port;
            add_core_translation(
                CoreCoord(translated_x, translated_y, CoreType::DRAM, CoordSystem::TRANSLATED),
                dram_cores[bank_order[index] * dram_grid_size.y + port]);
        }
    }
}

void BlackholeCoordinateManager::fill_pcie_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_pcie_default_noc0_translated_mapping();
        return;
    }
    for (const auto& [translated_coord, noc0_pair] : working_row_translations(
             pcie_cores,
             CoreType::PCIE,
             harvesting_masks.pcie_harvesting_mask,
             blackhole::pcie_translated_coordinate_start_x,
             blackhole::pcie_translated_coordinate_start_y)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

// The ARC keeps its NOC0 coordinates in translated space.
void BlackholeCoordinateManager::fill_arc_noc0_translated_mapping() { fill_arc_default_noc0_translated_mapping(); }

}  // namespace tt::umd
