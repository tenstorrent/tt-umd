// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/coordinates/coordinate_manager.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "coordinates/blackhole_coordinate_manager.hpp"
#include "coordinates/quasar_coordinate_manager.hpp"
#include "coordinates/wormhole_coordinate_manager.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

bool is_harvested(size_t harvesting_mask, size_t index) { return index < 64 && ((harvesting_mask >> index) & 1) != 0; }

// Index of value in a sorted list of the distinct values of one axis, i.e. the row or column a core
// sits in within its grid.
size_t axis_index(const std::vector<size_t>& axis_values, size_t value) {
    return std::distance(axis_values.begin(), std::lower_bound(axis_values.begin(), axis_values.end(), value));
}

std::vector<size_t> distinct_sorted(const std::vector<tt_xy_pair>& cores, size_t tt_xy_pair::*axis) {
    std::vector<size_t> values;
    values.reserve(cores.size());
    for (const tt_xy_pair& core : cores) {
        values.push_back(core.*axis);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

template <typename T>
uint32_t index_in(const std::vector<T>& values, T value) {
    auto it = std::find(values.begin(), values.end(), value);
    UMD_ASSERT(it != values.end(), error::RuntimeError, "Location is missing from the architecture's layout table.");
    return std::distance(values.begin(), it);
}

}  // namespace

CoordinateManager::CoordinateManager(
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
    noc_translation_enabled(noc_translation_enabled),
    harvesting_masks(harvesting_masks),
    tensix_grid_size(tensix_grid_size),
    tensix_cores(tensix_cores),
    dram_grid_size(dram_grid_size),
    dram_cores(dram_cores),
    num_eth_channels(eth_cores.size()),
    eth_cores(eth_cores),
    arc_grid_size(arc_grid_size),
    arc_cores(arc_cores),
    pcie_grid_size(pcie_grid_size),
    pcie_cores(pcie_cores),
    router_cores(router_cores),
    security_cores(security_cores),
    l2cpu_cores(l2cpu_cores),
    dispatch_cores(dispatch_cores),
    noc0_x_to_noc1_x(noc0_x_to_noc1_x),
    noc0_y_to_noc1_y(noc0_y_to_noc1_y) {}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch,
    const bool noc_translation_enabled,
    HarvestingMasks harvesting_masks,
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
    const std::vector<uint32_t>& noc0_y_to_noc1_y) {
    std::shared_ptr<CoordinateManager> coordinate_manager;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            coordinate_manager = std::make_shared<WormholeCoordinateManager>(
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
                noc0_y_to_noc1_y);
            break;
        case tt::ARCH::BLACKHOLE:
            coordinate_manager = std::make_shared<BlackholeCoordinateManager>(
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
                noc0_y_to_noc1_y);
            break;
        case tt::ARCH::QUASAR:
            coordinate_manager = std::make_shared<QuasarCoordinateManager>(
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
                noc0_y_to_noc1_y);
            break;
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("No coordinate manager exists for architecture {}.", arch_to_str(arch)));
    }

    // The translation tables are filled through virtual calls, so they can only be built once the
    // derived object exists.
    coordinate_manager->initialize();
    return coordinate_manager;
}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch,
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
    const BoardType board_type,
    const uint8_t asic_location) {
    const SocArchDescriptor arch_desc(arch);

    HarvestingMasks masks = harvesting_masks;
    if (arch == tt::ARCH::BLACKHOLE && masks.pcie_harvesting_mask == 0) {
        masks.pcie_harvesting_mask = BlackholeCoordinateManager::get_pcie_harvesting_mask(board_type, asic_location);
    }

    const std::vector<std::vector<tt_xy_pair>>& dram_channels = arch_desc.get_dram_cores();
    const tt_xy_pair dram_grid_size(dram_channels.size(), dram_channels.empty() ? 0 : dram_channels.front().size());

    return create_coordinate_manager(
        arch,
        noc_translation_enabled,
        masks,
        arch_desc.get_worker_grid_size(),
        arch_desc.get_tensix_cores(),
        dram_grid_size,
        flatten_vector(dram_channels),
        arch_desc.get_eth_cores(),
        SocArchDescriptor::calculate_grid_size(arch_desc.get_firmware_cores()),
        arch_desc.get_firmware_cores(),
        SocArchDescriptor::calculate_grid_size(arch_desc.get_pcie_cores()),
        arch_desc.get_pcie_cores(),
        arch_desc.get_router_cores(),
        arch_desc.get_security_cores(),
        arch_desc.get_l2cpu_cores(),
        arch_desc.get_dispatch_cores(),
        arch_desc.get_noc0_x_to_noc1_x(),
        arch_desc.get_noc0_y_to_noc1_y());
}

size_t CoordinateManager::get_num_harvested(const size_t harvesting_mask) {
    return std::bitset<64>(harvesting_mask).count();
}

std::vector<size_t> CoordinateManager::get_harvested_indices(const size_t harvesting_mask) {
    std::vector<size_t> indices;
    for (size_t index = 0; index < 64; index++) {
        if (is_harvested(harvesting_mask, index)) {
            indices.push_back(index);
        }
    }
    return indices;
}

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH arch, uint32_t tensix_harvesting_physical_layout) {
    // Firmware numbers Tensix rows (Wormhole) or columns (Blackhole) in its own physical order, given
    // by HARVESTING_NOC_LOCATIONS; the NOC0 layout numbers them in increasing NOC0 coordinate.
    const std::vector<uint32_t>* physical_locations = nullptr;
    const std::vector<uint32_t>* noc0_locations = nullptr;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            physical_locations = &wormhole::HARVESTING_NOC_LOCATIONS;
            noc0_locations = &wormhole::T6_Y_LOCATIONS;
            break;
        case tt::ARCH::BLACKHOLE:
            physical_locations = &blackhole::HARVESTING_NOC_LOCATIONS;
            noc0_locations = &blackhole::T6_X_LOCATIONS;
            break;
        default:
            return tensix_harvesting_physical_layout;
    }

    uint32_t noc0_layout = 0;
    for (size_t physical_index : get_harvested_indices(tensix_harvesting_physical_layout)) {
        if (physical_index >= physical_locations->size()) {
            continue;
        }
        noc0_layout |= 1u << index_in(*noc0_locations, physical_locations->at(physical_index));
    }
    return noc0_layout;
}

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask_to_noc0_coords(
    tt::ARCH arch, uint32_t tensix_harvesting_logical_layout) {
    // Re-keys the mask from Tensix row/column index to the NOC0 coordinate of that row/column.
    const std::vector<uint32_t>* noc0_locations = nullptr;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            noc0_locations = &wormhole::T6_Y_LOCATIONS;
            break;
        case tt::ARCH::BLACKHOLE:
            noc0_locations = &blackhole::T6_X_LOCATIONS;
            break;
        default:
            return tensix_harvesting_logical_layout;
    }

    uint32_t noc0_coords_mask = 0;
    for (size_t index : get_harvested_indices(tensix_harvesting_logical_layout)) {
        if (index < noc0_locations->size()) {
            noc0_coords_mask |= 1u << noc0_locations->at(index);
        }
    }
    return noc0_coords_mask;
}

uint32_t CoordinateManager::shuffle_l2cpu_harvesting_mask(tt::ARCH arch, uint32_t l2cpu_enabled_physical_layout) {
    if (arch != tt::ARCH::BLACKHOLE) {
        return 0;
    }

    // Firmware reports L2CPU clusters as an enable bitmap whose bit i is the cluster at NOC0 (8, y[i]).
    static const std::vector<size_t> L2CPU_PHYSICAL_Y = {3, 9, 5, 7};

    uint32_t harvesting_mask = 0;
    for (size_t physical_index = 0; physical_index < L2CPU_PHYSICAL_Y.size(); physical_index++) {
        if (is_harvested(l2cpu_enabled_physical_layout, physical_index)) {
            continue;
        }
        const tt_xy_pair core(blackhole::L2CPU_CORES_NOC0.front().x, L2CPU_PHYSICAL_Y[physical_index]);
        harvesting_mask |= 1u << index_in(blackhole::L2CPU_CORES_NOC0, core);
    }
    return harvesting_mask;
}

void CoordinateManager::initialize() {
    assert_coordinate_manager_constructor();

    translate_tensix_coords();
    translate_dram_coords();
    translate_eth_coords();
    translate_arc_coords();
    translate_pcie_coords();
    translate_router_coords();
    translate_security_coords();
    translate_l2cpu_coords();
    translate_dispatch_coords();

    add_noc1_to_noc0_mapping();
}

void CoordinateManager::assert_coordinate_manager_constructor() {}

void CoordinateManager::add_core_translation(const CoreCoord& core_coord, const tt_xy_pair& noc0_pair) {
    to_noc0_map[core_coord] = noc0_pair;
    from_noc0_map[{noc0_pair, core_coord.coord_system}] = core_coord;
    // Logical coordinates restart from 0 for every core type, so they alone don't identify a core.
    if (core_coord.coord_system != CoordSystem::LOGICAL) {
        to_core_type_map[{core_coord.to_pair(), core_coord.coord_system}] = core_coord;
    }
}

void CoordinateManager::add_noc1_to_noc0_mapping() {
    if (noc0_x_to_noc1_x.empty() || noc0_y_to_noc1_y.empty()) {
        return;
    }

    std::vector<std::pair<CoreCoord, tt_xy_pair>> noc0_cores;
    for (const auto& [core, noc0_pair] : to_noc0_map) {
        if (core.coord_system == CoordSystem::NOC0) {
            noc0_cores.emplace_back(core, noc0_pair);
        }
    }

    for (const auto& [core, noc0_pair] : noc0_cores) {
        if (noc0_pair.x >= noc0_x_to_noc1_x.size() || noc0_pair.y >= noc0_y_to_noc1_y.size()) {
            continue;
        }
        const tt_xy_pair noc1_pair(noc0_x_to_noc1_x[noc0_pair.x], noc0_y_to_noc1_y[noc0_pair.y]);
        add_core_translation(CoreCoord(noc1_pair, core.core_type, CoordSystem::NOC1), noc0_pair);
    }
}

void CoordinateManager::translate_tensix_coords() {
    // Tensix rows are the harvesting unit unless an architecture says otherwise.
    const std::vector<size_t> rows = distinct_sorted(tensix_cores, &tt_xy_pair::y);
    const std::vector<size_t> columns = distinct_sorted(tensix_cores, &tt_xy_pair::x);

    for (const tt_xy_pair& core : tensix_cores) {
        const size_t row = axis_index(rows, core.y);
        add_core_translation(CoreCoord(core, CoreType::TENSIX, CoordSystem::NOC0), core);
        if (is_harvested(harvesting_masks.tensix_harvesting_mask, row)) {
            continue;
        }

        size_t logical_y = 0;
        for (size_t previous_row = 0; previous_row < row; previous_row++) {
            logical_y += is_harvested(harvesting_masks.tensix_harvesting_mask, previous_row) ? 0 : 1;
        }
        const size_t logical_x = axis_index(columns, core.x);
        add_core_translation(CoreCoord(logical_x, logical_y, CoreType::TENSIX, CoordSystem::LOGICAL), core);
    }

    fill_tensix_noc0_translated_mapping();
}

void CoordinateManager::translate_dram_coords() {
    size_t logical_bank = 0;
    for (size_t bank = 0; bank < dram_grid_size.x; bank++) {
        const bool harvested = is_harvested(harvesting_masks.dram_harvesting_mask, bank);
        for (size_t port = 0; port < dram_grid_size.y; port++) {
            const size_t index = bank * dram_grid_size.y + port;
            if (index >= dram_cores.size()) {
                break;
            }
            const tt_xy_pair& core = dram_cores[index];
            add_core_translation(CoreCoord(core, CoreType::DRAM, CoordSystem::NOC0), core);
            if (!harvested) {
                add_core_translation(CoreCoord(logical_bank, port, CoreType::DRAM, CoordSystem::LOGICAL), core);
            }
        }
        logical_bank += harvested ? 0 : 1;
    }

    fill_dram_noc0_translated_mapping();
}

void CoordinateManager::translate_eth_coords() {
    // The logical Y of an ETH core is its channel, counted over the cores that aren't harvested.
    size_t channel = 0;
    for (size_t index = 0; index < eth_cores.size(); index++) {
        const tt_xy_pair& core = eth_cores[index];
        add_core_translation(CoreCoord(core, CoreType::ETH, CoordSystem::NOC0), core);
        if (!is_harvested(harvesting_masks.eth_harvesting_mask, index)) {
            add_core_translation(CoreCoord(0, channel++, CoreType::ETH, CoordSystem::LOGICAL), core);
        }
    }

    fill_eth_noc0_translated_mapping();
}

void CoordinateManager::translate_arc_coords() {
    for (size_t index = 0; index < arc_cores.size(); index++) {
        const tt_xy_pair& core = arc_cores[index];
        const size_t grid_width = std::max<size_t>(arc_grid_size.x, 1);
        add_core_translation(CoreCoord(core, CoreType::ARC, CoordSystem::NOC0), core);
        add_core_translation(
            CoreCoord(index % grid_width, index / grid_width, CoreType::ARC, CoordSystem::LOGICAL), core);
    }

    fill_arc_noc0_translated_mapping();
}

void CoordinateManager::translate_pcie_coords() {
    size_t logical_index = 0;
    for (size_t index = 0; index < pcie_cores.size(); index++) {
        const tt_xy_pair& core = pcie_cores[index];
        add_core_translation(CoreCoord(core, CoreType::PCIE, CoordSystem::NOC0), core);
        if (!is_harvested(harvesting_masks.pcie_harvesting_mask, index)) {
            add_core_translation(CoreCoord(logical_index++, 0, CoreType::PCIE, CoordSystem::LOGICAL), core);
        }
    }

    fill_pcie_noc0_translated_mapping();
}

void CoordinateManager::translate_router_coords() {
    for (size_t index = 0; index < router_cores.size(); index++) {
        const tt_xy_pair& core = router_cores[index];
        add_core_translation(CoreCoord(core, CoreType::ROUTER_ONLY, CoordSystem::NOC0), core);
        add_core_translation(CoreCoord(core, CoreType::ROUTER_ONLY, CoordSystem::TRANSLATED), core);
        add_core_translation(CoreCoord(index, 0, CoreType::ROUTER_ONLY, CoordSystem::LOGICAL), core);
    }
}

void CoordinateManager::translate_security_coords() {
    for (size_t index = 0; index < security_cores.size(); index++) {
        const tt_xy_pair& core = security_cores[index];
        add_core_translation(CoreCoord(core, CoreType::SECURITY, CoordSystem::NOC0), core);
        add_core_translation(CoreCoord(core, CoreType::SECURITY, CoordSystem::TRANSLATED), core);
        add_core_translation(CoreCoord(index, 0, CoreType::SECURITY, CoordSystem::LOGICAL), core);
    }
}

void CoordinateManager::translate_l2cpu_coords() {
    size_t logical_index = 0;
    for (size_t index = 0; index < l2cpu_cores.size(); index++) {
        const tt_xy_pair& core = l2cpu_cores[index];
        add_core_translation(CoreCoord(core, CoreType::L2CPU, CoordSystem::NOC0), core);
        add_core_translation(CoreCoord(core, CoreType::L2CPU, CoordSystem::TRANSLATED), core);
        if (!is_harvested(harvesting_masks.l2cpu_harvesting_mask, index)) {
            add_core_translation(CoreCoord(logical_index++, 0, CoreType::L2CPU, CoordSystem::LOGICAL), core);
        }
    }
}

void CoordinateManager::translate_dispatch_coords() {
    for (size_t index = 0; index < dispatch_cores.size(); index++) {
        const tt_xy_pair& core = dispatch_cores[index];
        add_core_translation(CoreCoord(core, CoreType::DISPATCH, CoordSystem::NOC0), core);
        add_core_translation(CoreCoord(core, CoreType::DISPATCH, CoordSystem::TRANSLATED), core);
        add_core_translation(CoreCoord(index, 0, CoreType::DISPATCH, CoordSystem::LOGICAL), core);
    }
}

void CoordinateManager::fill_tensix_default_noc0_translated_mapping() {
    for (const tt_xy_pair& core : tensix_cores) {
        add_core_translation(CoreCoord(core, CoreType::TENSIX, CoordSystem::TRANSLATED), core);
    }
}

void CoordinateManager::fill_eth_default_noc0_translated_mapping() {
    for (const tt_xy_pair& core : eth_cores) {
        add_core_translation(CoreCoord(core, CoreType::ETH, CoordSystem::TRANSLATED), core);
    }
}

void CoordinateManager::fill_dram_default_noc0_translated_mapping() {
    for (const tt_xy_pair& core : dram_cores) {
        add_core_translation(CoreCoord(core, CoreType::DRAM, CoordSystem::TRANSLATED), core);
    }
}

void CoordinateManager::fill_pcie_default_noc0_translated_mapping() {
    for (const tt_xy_pair& core : pcie_cores) {
        add_core_translation(CoreCoord(core, CoreType::PCIE, CoordSystem::TRANSLATED), core);
    }
}

void CoordinateManager::fill_arc_default_noc0_translated_mapping() {
    for (const tt_xy_pair& core : arc_cores) {
        add_core_translation(CoreCoord(core, CoreType::ARC, CoordSystem::TRANSLATED), core);
    }
}

CoreCoord CoordinateManager::translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const {
    UMD_ASSERT(
        core_coord.coord_system != CoordSystem::LITERAL,
        error::RuntimeError,
        fmt::format("Core {} is LITERAL: it names no core, so it cannot be translated.", core_coord.str()));
    UMD_ASSERT(
        coord_system != CoordSystem::LITERAL,
        error::RuntimeError,
        fmt::format("Core {} cannot be translated to LITERAL, which is not a coordinate system.", core_coord.str()));

    auto noc0_it = to_noc0_map.find(core_coord);
    UMD_ASSERT(
        noc0_it != to_noc0_map.end(), error::RuntimeError, fmt::format("Core {} does not exist.", core_coord.str()));

    auto target_it = from_noc0_map.find({noc0_it->second, coord_system});
    UMD_ASSERT(
        target_it != from_noc0_map.end(),
        error::RuntimeError,
        fmt::format("Core {} has no {} coordinates.", core_coord.str(), to_str(coord_system)));
    return target_it->second;
}

CoreCoord CoordinateManager::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    auto it = to_core_type_map.find({core, coord_system});
    UMD_ASSERT(
        it != to_core_type_map.end(),
        error::RuntimeError,
        fmt::format("No core at {} in {} coordinates.", core.str(), to_str(coord_system)));
    return it->second;
}

CoreCoord CoordinateManager::translate_coord_to(
    const tt_xy_pair core, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    return translate_coord_to(get_coord_at(core, input_coord_system), target_coord_system);
}

const std::vector<tt_xy_pair>& CoordinateManager::get_noc0_pairs(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return tensix_cores;
        case CoreType::DRAM:
            return dram_cores;
        case CoreType::ETH:
            return eth_cores;
        case CoreType::ARC:
            return arc_cores;
        case CoreType::PCIE:
            return pcie_cores;
        case CoreType::ROUTER_ONLY:
            return router_cores;
        case CoreType::SECURITY:
            return security_cores;
        case CoreType::L2CPU:
            return l2cpu_cores;
        case CoreType::DISPATCH:
            return dispatch_cores;
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Core type {} has no cores.", to_str(core_type)));
    }
}

std::vector<CoreCoord> CoordinateManager::get_all_noc0_cores(const CoreType core_type) const {
    std::vector<CoreCoord> cores;
    for (const tt_xy_pair& core : get_noc0_pairs(core_type)) {
        cores.emplace_back(core, core_type, CoordSystem::NOC0);
    }
    return cores;
}

namespace {

// Splits the NOC0 cores of one type by whether they have a logical coordinate, which only cores that
// aren't harvested get.
std::vector<CoreCoord> select_cores(
    const std::vector<tt_xy_pair>& noc0_pairs,
    CoreType core_type,
    const std::map<std::pair<tt_xy_pair, CoordSystem>, CoreCoord>& from_noc0_map,
    bool harvested) {
    std::vector<CoreCoord> cores;
    for (const tt_xy_pair& core : noc0_pairs) {
        const bool has_logical = from_noc0_map.count({core, CoordSystem::LOGICAL}) != 0;
        if (has_logical != harvested) {
            cores.emplace_back(core, core_type, CoordSystem::NOC0);
        }
    }
    return cores;
}

}  // namespace

std::vector<CoreCoord> CoordinateManager::get_tensix_cores() const {
    return select_cores(tensix_cores, CoreType::TENSIX, from_noc0_map, false);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_tensix_cores() const {
    return select_cores(tensix_cores, CoreType::TENSIX, from_noc0_map, true);
}

std::vector<CoreCoord> CoordinateManager::get_dram_cores() const {
    return select_cores(dram_cores, CoreType::DRAM, from_noc0_map, false);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_dram_cores() const {
    return select_cores(dram_cores, CoreType::DRAM, from_noc0_map, true);
}

std::vector<CoreCoord> CoordinateManager::get_eth_cores() const {
    return select_cores(eth_cores, CoreType::ETH, from_noc0_map, false);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_eth_cores() const {
    return select_cores(eth_cores, CoreType::ETH, from_noc0_map, true);
}

std::vector<CoreCoord> CoordinateManager::get_pcie_cores() const {
    return select_cores(pcie_cores, CoreType::PCIE, from_noc0_map, false);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_pcie_cores() const {
    return select_cores(pcie_cores, CoreType::PCIE, from_noc0_map, true);
}

std::vector<CoreCoord> CoordinateManager::get_l2cpu_cores() const {
    return select_cores(l2cpu_cores, CoreType::L2CPU, from_noc0_map, false);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_l2cpu_cores() const {
    return select_cores(l2cpu_cores, CoreType::L2CPU, from_noc0_map, true);
}

std::vector<CoreCoord> CoordinateManager::get_dispatch_cores() const { return get_all_noc0_cores(CoreType::DISPATCH); }

std::vector<CoreCoord> CoordinateManager::get_harvested_dispatch_cores() const { return {}; }

tt_xy_pair CoordinateManager::get_dram_grid_size() const {
    return {dram_grid_size.x - get_num_harvested(harvesting_masks.dram_harvesting_mask), dram_grid_size.y};
}

tt_xy_pair CoordinateManager::get_harvested_tensix_grid_size() const {
    return {tensix_grid_size.x, get_num_harvested(harvesting_masks.tensix_harvesting_mask)};
}

tt_xy_pair CoordinateManager::get_harvested_dram_grid_size() const {
    return {get_num_harvested(harvesting_masks.dram_harvesting_mask), dram_grid_size.y};
}

std::vector<CoreCoord> CoordinateManager::get_cores(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_tensix_cores();
        case CoreType::DRAM:
            return get_dram_cores();
        case CoreType::ETH:
            return get_eth_cores();
        case CoreType::PCIE:
            return get_pcie_cores();
        case CoreType::L2CPU:
            return get_l2cpu_cores();
        case CoreType::DISPATCH:
            return get_dispatch_cores();
        case CoreType::ARC:
        case CoreType::ROUTER_ONLY:
        case CoreType::SECURITY:
            return get_all_noc0_cores(core_type);
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Core type {} has no cores.", to_str(core_type)));
    }
}

std::vector<CoreCoord> CoordinateManager::get_harvested_cores(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_harvested_tensix_cores();
        case CoreType::DRAM:
            return get_harvested_dram_cores();
        case CoreType::ETH:
            return get_harvested_eth_cores();
        case CoreType::PCIE:
            return get_harvested_pcie_cores();
        case CoreType::L2CPU:
            return get_harvested_l2cpu_cores();
        case CoreType::DISPATCH:
            return get_harvested_dispatch_cores();
        case CoreType::ARC:
        case CoreType::ROUTER_ONLY:
        case CoreType::SECURITY:
            return {};
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Core type {} has no cores.", to_str(core_type)));
    }
}

tt_xy_pair CoordinateManager::get_grid_size(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_tensix_grid_size();
        case CoreType::DRAM:
            return get_dram_grid_size();
        case CoreType::ETH:
            return {1, get_num_eth_channels()};
        case CoreType::ARC:
            return arc_grid_size;
        case CoreType::PCIE:
            return {get_pcie_cores().size(), 1};
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Core type {} is not laid out in a grid.", to_str(core_type)));
    }
}

tt_xy_pair CoordinateManager::get_harvested_grid_size(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_harvested_tensix_grid_size();
        case CoreType::DRAM:
            return get_harvested_dram_grid_size();
        case CoreType::ETH:
            return {1, get_num_harvested_eth_channels()};
        case CoreType::PCIE:
            return {get_harvested_pcie_cores().size(), 1};
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Core type {} is not laid out in a grid.", to_str(core_type)));
    }
}

HarvestingMasks CoordinateManager::get_harvesting_masks() const { return harvesting_masks; }

uint32_t CoordinateManager::get_num_eth_channels() const { return num_eth_channels - get_num_harvested_eth_channels(); }

uint32_t CoordinateManager::get_num_harvested_eth_channels() const { return get_harvested_eth_cores().size(); }

}  // namespace tt::umd
