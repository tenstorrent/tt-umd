// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "coordinates/blackhole_coordinate_manager.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <set>
#include <vector>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

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

size_t BlackholeCoordinateManager::get_pcie_harvesting_mask(BoardType board_type, uint8_t asic_location) {
    if (board_type != BoardType::P100 && board_type != BoardType::P150 && board_type != BoardType::P300) {
        return 0;
    }
    // Type 1 chips reach the host through the PCIe core at (11, 0), type 2 through the one at (2, 0).
    return get_blackhole_chip_type(board_type, asic_location) == BlackholeChipType::Type1 ? 0b01 : 0b10;
}

void BlackholeCoordinateManager::assert_coordinate_manager_constructor() {
    // Every Blackhole chip has at least two ETH cores disabled, and the translated ETH row only has
    // room for twelve.
    if (eth_cores.size() == blackhole::NUM_ETH_CHANNELS) {
        UMD_ASSERT(
            get_num_harvested(harvesting_masks.eth_harvesting_mask) >= 2,
            error::RuntimeError,
            fmt::format(
                "Blackhole ETH harvesting mask {:#x} harvests fewer than 2 ETH cores.",
                harvesting_masks.eth_harvesting_mask));
    }
}

namespace {

bool is_harvested(size_t harvesting_mask, size_t index) { return index < 64 && ((harvesting_mask >> index) & 1) != 0; }

std::vector<size_t> distinct_sorted_x(const std::vector<tt_xy_pair>& cores) {
    std::vector<size_t> values;
    for (const tt_xy_pair& core : cores) {
        values.push_back(core.x);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

size_t index_of(const std::vector<size_t>& values, size_t value) {
    return std::distance(values.begin(), std::find(values.begin(), values.end(), value));
}

// Position of a Tensix column in the firmware's harvesting order, which numbers the columns
// outside-in rather than by NOC0 X.
size_t firmware_column_index(size_t noc0_x) {
    const auto& order = blackhole::HARVESTING_NOC_LOCATIONS;
    return std::distance(order.begin(), std::find(order.begin(), order.end(), noc0_x));
}

}  // namespace

void BlackholeCoordinateManager::translate_tensix_coords() {
    // Blackhole harvests whole Tensix columns.
    const std::vector<size_t> columns = distinct_sorted_x(tensix_cores);
    std::vector<size_t> rows;
    for (const tt_xy_pair& core : tensix_cores) {
        rows.push_back(core.y);
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    for (const tt_xy_pair& core : tensix_cores) {
        const size_t column = index_of(columns, core.x);
        add_core_translation(CoreCoord(core, CoreType::TENSIX, CoordSystem::NOC0), core);
        if (is_harvested(harvesting_masks.tensix_harvesting_mask, column)) {
            continue;
        }

        size_t logical_x = 0;
        for (size_t previous_column = 0; previous_column < column; previous_column++) {
            logical_x += is_harvested(harvesting_masks.tensix_harvesting_mask, previous_column) ? 0 : 1;
        }
        add_core_translation(
            CoreCoord(logical_x, index_of(rows, core.y), CoreType::TENSIX, CoordSystem::LOGICAL), core);
    }

    fill_tensix_noc0_translated_mapping();
}

tt_xy_pair BlackholeCoordinateManager::get_tensix_grid_size() const {
    return {tensix_grid_size.x - get_num_harvested(harvesting_masks.tensix_harvesting_mask), tensix_grid_size.y};
}

tt_xy_pair BlackholeCoordinateManager::get_harvested_tensix_grid_size() const {
    return {get_num_harvested(harvesting_masks.tensix_harvesting_mask), tensix_grid_size.y};
}

void BlackholeCoordinateManager::fill_tensix_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_tensix_default_noc0_translated_mapping();
        return;
    }

    // The translated X slots are the NOC0 Tensix columns themselves. Columns that aren't harvested
    // take them from the left in NOC0 order; harvested columns take them from the right, in firmware
    // harvesting order. Rows are not translated.
    const std::vector<size_t> columns = distinct_sorted_x(tensix_cores);
    std::vector<size_t> unharvested_columns;
    std::vector<size_t> harvested_columns;
    for (size_t column = 0; column < columns.size(); column++) {
        (is_harvested(harvesting_masks.tensix_harvesting_mask, column) ? harvested_columns : unharvested_columns)
            .push_back(columns[column]);
    }
    std::sort(harvested_columns.begin(), harvested_columns.end(), [](size_t a, size_t b) {
        return firmware_column_index(a) < firmware_column_index(b);
    });

    std::map<size_t, size_t> noc0_x_to_translated_x;
    for (size_t slot = 0; slot < unharvested_columns.size(); slot++) {
        noc0_x_to_translated_x[unharvested_columns[slot]] = columns[slot];
    }
    for (size_t position = 0; position < harvested_columns.size(); position++) {
        noc0_x_to_translated_x[harvested_columns[position]] = columns[columns.size() - 1 - position];
    }

    for (const tt_xy_pair& core : tensix_cores) {
        add_core_translation(
            CoreCoord(noc0_x_to_translated_x.at(core.x), core.y, CoreType::TENSIX, CoordSystem::TRANSLATED), core);
    }
}

void BlackholeCoordinateManager::fill_eth_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_eth_default_noc0_translated_mapping();
        return;
    }

    // The firmware gives two ETH cores no translated slot: the highest-numbered harvested one among
    // channels 4-6 and among channels 7-9. Should a group have none harvested, the lowest-numbered
    // harvested cores stand in for it. Every other core, harvested or not, gets the next slot in the
    // translated ETH row in channel order, and the two left out keep their NOC0 coordinates.
    std::set<size_t> skipped;
    for (const std::vector<size_t>& group : {std::vector<size_t>{4, 5, 6}, std::vector<size_t>{7, 8, 9}}) {
        for (auto it = group.rbegin(); it != group.rend(); ++it) {
            if (*it < eth_cores.size() && is_harvested(harvesting_masks.eth_harvesting_mask, *it)) {
                skipped.insert(*it);
                break;
            }
        }
    }
    for (size_t index = 0; index < eth_cores.size() && skipped.size() < 2; index++) {
        if (is_harvested(harvesting_masks.eth_harvesting_mask, index)) {
            skipped.insert(index);
        }
    }

    size_t translated_x = blackhole::eth_translated_coordinate_start_x;
    for (size_t index = 0; index < eth_cores.size(); index++) {
        const tt_xy_pair& core = eth_cores[index];
        if (skipped.count(index) != 0) {
            add_core_translation(CoreCoord(core, CoreType::ETH, CoordSystem::TRANSLATED), core);
            continue;
        }
        add_core_translation(
            CoreCoord(
                translated_x++, blackhole::eth_translated_coordinate_start_y, CoreType::ETH, CoordSystem::TRANSLATED),
            core);
    }
}

void BlackholeCoordinateManager::fill_dram_noc0_translated_mapping() {
    if (!noc_translation_enabled || dram_grid_size.x != blackhole::NUM_DRAM_BANKS ||
        dram_grid_size.y != blackhole::NUM_NOC_PORTS_PER_DRAM_BANK) {
        fill_dram_default_noc0_translated_mapping();
        return;
    }

    // Banks 0-3 sit in the west DRAM column and banks 4-7 in the east one, bank i and i + 4 sharing
    // NOC0 rows. Translated X 17 is the west column and 18 the east one, except that the column of the
    // lowest-numbered harvested bank always becomes X 18. Translated Y numbers the four rows in bank
    // order, three ports each, with the row of that harvested bank moved last.
    constexpr size_t BANKS_PER_COLUMN = blackhole::NUM_DRAM_BANKS / 2;
    const std::vector<size_t> harvested_banks = get_harvested_indices(harvesting_masks.dram_harvesting_mask);
    const bool has_harvested_bank = !harvested_banks.empty() && harvested_banks.front() < blackhole::NUM_DRAM_BANKS;

    size_t west_x = blackhole::dram_translated_coordinate_start_x;
    size_t east_x = blackhole::dram_translated_coordinate_start_x + 1;
    std::vector<size_t> row_order;
    for (size_t row = 0; row < BANKS_PER_COLUMN; row++) {
        row_order.push_back(row);
    }
    if (has_harvested_bank) {
        const size_t harvested_bank = harvested_banks.front();
        if (harvested_bank < BANKS_PER_COLUMN) {
            std::swap(west_x, east_x);
        }
        const size_t harvested_row = harvested_bank % BANKS_PER_COLUMN;
        row_order.erase(std::find(row_order.begin(), row_order.end(), harvested_row));
        row_order.push_back(harvested_row);
    }

    for (size_t bank = 0; bank < blackhole::NUM_DRAM_BANKS; bank++) {
        const size_t row_position = index_of(row_order, bank % BANKS_PER_COLUMN);
        const size_t translated_x = bank < BANKS_PER_COLUMN ? west_x : east_x;
        for (size_t port = 0; port < blackhole::NUM_NOC_PORTS_PER_DRAM_BANK; port++) {
            const size_t translated_y = blackhole::dram_translated_coordinate_start_y +
                                        row_position * blackhole::NUM_NOC_PORTS_PER_DRAM_BANK + port;
            add_core_translation(
                CoreCoord(translated_x, translated_y, CoreType::DRAM, CoordSystem::TRANSLATED),
                dram_cores[bank * blackhole::NUM_NOC_PORTS_PER_DRAM_BANK + port]);
        }
    }
}

void BlackholeCoordinateManager::fill_pcie_noc0_translated_mapping() {
    if (!noc_translation_enabled) {
        fill_pcie_default_noc0_translated_mapping();
        return;
    }

    // One translated coordinate reaches the PCIe core connected to the host; the other PCIe core is
    // only reachable at its NOC0 coordinates.
    bool host_core_assigned = false;
    for (size_t index = 0; index < pcie_cores.size(); index++) {
        const tt_xy_pair& core = pcie_cores[index];
        if (!host_core_assigned && !is_harvested(harvesting_masks.pcie_harvesting_mask, index)) {
            add_core_translation(
                CoreCoord(
                    blackhole::pcie_translated_coordinate_start_x,
                    blackhole::pcie_translated_coordinate_start_y,
                    CoreType::PCIE,
                    CoordSystem::TRANSLATED),
                core);
            host_core_assigned = true;
            continue;
        }
        add_core_translation(CoreCoord(core, CoreType::PCIE, CoordSystem::TRANSLATED), core);
    }
}

// ARC sits in the X = 8 column, which the translation tables map to itself.
void BlackholeCoordinateManager::fill_arc_noc0_translated_mapping() { fill_arc_default_noc0_translated_mapping(); }

}  // namespace tt::umd
