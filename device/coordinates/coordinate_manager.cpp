// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "api/umd/device/coordinates/coordinate_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/coordinates/blackhole_coordinate_manager.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/coordinates/wormhole_coordinate_manager.hpp"

namespace tt::umd {

CoordinateManager::CoordinateManager(
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
    noc0_x_to_noc1_x(noc0_x_to_noc1_x),
    noc0_y_to_noc1_y(noc0_y_to_noc1_y) {}

void CoordinateManager::initialize() {
    this->assert_coordinate_manager_constructor();
    this->identity_map_noc0_cores();
    this->translate_tensix_coords();
    this->translate_dram_coords();
    this->translate_eth_coords();
    this->translate_arc_coords();
    this->translate_pcie_coords();
    this->translate_router_coords();
    this->translate_security_coords();
    this->translate_l2cpu_coords();
    this->add_noc1_to_noc0_mapping();
}

void CoordinateManager::assert_coordinate_manager_constructor() {
    if (harvesting_masks.dram_harvesting_mask != 0) {
        throw std::runtime_error("DRAM harvesting is supported only for Blackhole");
    }

    if (harvesting_masks.eth_harvesting_mask != 0) {
        throw std::runtime_error("ETH harvesting is supported only for Blackhole");
    }
}

void CoordinateManager::add_core_translation(const CoreCoord& core_coord, const tt_xy_pair& noc0_pair) {
    to_noc0_map.insert({core_coord, noc0_pair});
    from_noc0_map.insert({{{noc0_pair.x, noc0_pair.y}, core_coord.coord_system}, core_coord});
    if (core_coord.coord_system != CoordSystem::LOGICAL) {
        to_core_type_map.insert({{{core_coord.x, core_coord.y}, core_coord.coord_system}, core_coord});
    }
}

void CoordinateManager::identity_map_noc0_cores() {
    for (auto& core : tensix_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::TENSIX, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }

    for (auto& core : dram_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::DRAM, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }

    for (auto& core : eth_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::ETH, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }

    for (auto& core : arc_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::ARC, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }

    for (auto& core : pcie_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::PCIE, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }

    for (auto& core : router_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::ROUTER_ONLY, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }

    for (auto& core : security_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::SECURITY, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }

    for (auto& core : l2cpu_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::L2CPU, CoordSystem::NOC0);
        add_core_translation(core_coord, core);
    }
}

CoreCoord CoordinateManager::translate_coord_to(
    const CoreCoord core_coord, const CoordSystem target_coord_system) const {
    auto noc0_coord_it = to_noc0_map.find(core_coord);
    if (noc0_coord_it == to_noc0_map.end()) {
        throw std::runtime_error(fmt::format(
            "No core coordinate found at location: ({}, {}, {}, {})",
            core_coord.x,
            core_coord.y,
            to_str(core_coord.core_type),
            to_str(core_coord.coord_system)));
    }

    tt_xy_pair const noc0_coord = noc0_coord_it->second;
    auto coord_it = from_noc0_map.find({noc0_coord, target_coord_system});
    if (coord_it == from_noc0_map.end()) {
        throw std::runtime_error(fmt::format(
            "No core coordinate found for system {} at location: ({}, {}, {}, {})",
            to_str(target_coord_system),
            core_coord.x,
            core_coord.y,
            to_str(core_coord.core_type),
            to_str(core_coord.coord_system)));
    }
    return coord_it->second;
}

CoreCoord CoordinateManager::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    if (coord_system == CoordSystem::LOGICAL) {
        throw std::runtime_error("Coordinate is ambiguous for logical system.");
    }

    auto coord_it = to_core_type_map.find({core, coord_system});
    if (coord_it == to_core_type_map.end()) {
        throw std::runtime_error(fmt::format(
            "No core type found for system {} at location: ({}, {})", to_str(coord_system), core.x, core.y));
    }
    return coord_it->second;
}

CoreCoord CoordinateManager::translate_coord_to(
    const tt_xy_pair core, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    CoreCoord const core_coord = get_coord_at(core, input_coord_system);
    return translate_coord_to(core_coord, target_coord_system);
}

void CoordinateManager::translate_tensix_coords() {
    if (CoordinateManager::get_num_harvested(harvesting_masks.tensix_harvesting_mask) > tensix_grid_size.y) {
        harvesting_masks.tensix_harvesting_mask = 0;
    }
    size_t const grid_size_x = tensix_grid_size.x;
    size_t const grid_size_y = tensix_grid_size.y;

    size_t logical_y = 0;
    for (size_t y = 0; y < grid_size_y; y++) {
        if (!(harvesting_masks.tensix_harvesting_mask & (1 << y))) {
            for (size_t x = 0; x < grid_size_x; x++) {
                const tt_xy_pair& tensix_core = tensix_cores[y * grid_size_x + x];

                CoreCoord const logical_coord = CoreCoord(x, logical_y, CoreType::TENSIX, CoordSystem::LOGICAL);
                add_core_translation(logical_coord, tensix_core);
            }
            logical_y++;
        }
    }

    if (noc_translation_enabled) {
        fill_tensix_noc0_translated_mapping();
    } else {
        fill_tensix_default_noc0_translated_mapping();
    }
}

void CoordinateManager::fill_tensix_default_noc0_translated_mapping() {
    for (tt_xy_pair const noc0_core : tensix_cores) {
        CoreCoord const translated_coord = CoreCoord(noc0_core, CoreType::TENSIX, CoordSystem::TRANSLATED);
        add_core_translation(translated_coord, noc0_core);
    }
}

void CoordinateManager::translate_dram_coords() {
    for (size_t x = 0; x < dram_grid_size.x; x++) {
        for (size_t y = 0; y < dram_grid_size.y; y++) {
            const tt_xy_pair dram_core = dram_cores[x * dram_grid_size.y + y];

            CoreCoord const logical_coord = CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);

            add_core_translation(logical_coord, dram_core);
        }
    }

    if (noc_translation_enabled) {
        fill_dram_noc0_translated_mapping();
    } else {
        fill_dram_default_noc0_translated_mapping();
    }
}

void CoordinateManager::translate_eth_coords() {
    for (size_t eth_channel = 0; eth_channel < eth_cores.size(); eth_channel++) {
        const tt_xy_pair eth_core = eth_cores[eth_channel];

        CoreCoord const logical_coord = CoreCoord(0, eth_channel, CoreType::ETH, CoordSystem::LOGICAL);

        add_core_translation(logical_coord, eth_core);
    }

    if (noc_translation_enabled) {
        fill_eth_noc0_translated_mapping();
    } else {
        fill_eth_default_noc0_translated_mapping();
    }
}

void CoordinateManager::translate_arc_coords() {
    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            const tt_xy_pair arc_core = arc_cores[x * arc_grid_size.y + y];

            CoreCoord const logical_coord = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);

            add_core_translation(logical_coord, arc_core);
        }
    }

    if (noc_translation_enabled) {
        fill_arc_noc0_translated_mapping();
    } else {
        fill_arc_default_noc0_translated_mapping();
    }
}

void CoordinateManager::translate_pcie_coords() {
    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            const tt_xy_pair pcie_core = pcie_cores[x * pcie_grid_size.y + y];
            CoreCoord const logical_coord = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);

            add_core_translation(logical_coord, pcie_core);
        }
    }

    if (noc_translation_enabled) {
        fill_pcie_noc0_translated_mapping();
    } else {
        fill_pcie_default_noc0_translated_mapping();
    }
}

void CoordinateManager::translate_router_coords() {
    // Just do identity mapping for translated router coordinates.
    // No logical coordinates available for router cores.
    for (tt_xy_pair const router_core : router_cores) {
        CoreCoord const translated_coord = CoreCoord(router_core, CoreType::ROUTER_ONLY, CoordSystem::TRANSLATED);

        add_core_translation(translated_coord, router_core);
    }
}

void CoordinateManager::translate_security_coords() {
    // Just do identity mapping for translated SECURITY coordinates.
    // No logical coordinates available for SECURITY cores.
    for (tt_xy_pair const security_core : security_cores) {
        CoreCoord const translated_coord = CoreCoord(security_core, CoreType::SECURITY, CoordSystem::TRANSLATED);

        add_core_translation(translated_coord, security_core);
    }
}

void CoordinateManager::translate_l2cpu_coords() {
    // Just do identity mapping for translated L2CPU coordinates.
    // No logical coordinates available for L2CPU cores.
    for (tt_xy_pair const l2cpu_core : l2cpu_cores) {
        CoreCoord const translated_coord = CoreCoord(l2cpu_core, CoreType::L2CPU, CoordSystem::TRANSLATED);

        add_core_translation(translated_coord, l2cpu_core);
    }
}

void CoordinateManager::fill_eth_default_noc0_translated_mapping() {
    for (size_t eth_channel = 0; eth_channel < num_eth_channels; eth_channel++) {
        const tt_xy_pair noc0_pair = eth_cores[eth_channel];
        const size_t translated_x = noc0_pair.x;
        const size_t translated_y = noc0_pair.y;

        CoreCoord const translated_coord = CoreCoord(translated_x, translated_y, CoreType::ETH, CoordSystem::TRANSLATED);

        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::fill_dram_default_noc0_translated_mapping() {
    for (size_t x = 0; x < dram_grid_size.x; x++) {
        for (size_t y = 0; y < dram_grid_size.y; y++) {
            CoreCoord const logical_coord = CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);
            const tt_xy_pair noc0_pair = to_noc0_map[logical_coord];
            const size_t translated_x = noc0_pair.x;
            const size_t translated_y = noc0_pair.y;

            CoreCoord const translated_coord = CoreCoord(translated_x, translated_y, CoreType::DRAM, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, noc0_pair);
        }
    }
}

void CoordinateManager::fill_pcie_default_noc0_translated_mapping() {
    for (auto noc0_pair : pcie_cores) {
        const size_t translated_x = noc0_pair.x;
        const size_t translated_y = noc0_pair.y;

        CoreCoord const translated_coord = CoreCoord(translated_x, translated_y, CoreType::PCIE, CoordSystem::TRANSLATED);

        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::fill_arc_default_noc0_translated_mapping() {
    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            CoreCoord const logical_coord = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);
            const tt_xy_pair noc0_pair = to_noc0_map[logical_coord];
            const size_t translated_x = noc0_pair.x;
            const size_t translated_y = noc0_pair.y;

            CoreCoord const translated_coord = CoreCoord(translated_x, translated_y, CoreType::ARC, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, noc0_pair);
        }
    }
}

HarvestingMasks CoordinateManager::get_harvesting_masks() const { return harvesting_masks; }

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH arch, uint32_t tensix_harvesting_physical_layout) {
    std::vector<uint32_t> harvesting_locations =
        architecture_implementation::create(arch)->get_harvesting_noc_locations();

    std::vector<uint32_t> sorted_harvesting_locations = harvesting_locations;
    std::sort(sorted_harvesting_locations.begin(), sorted_harvesting_locations.end());
    size_t new_harvesting_mask = 0;
    uint32_t pos = 0;
    while (tensix_harvesting_physical_layout > 0) {
        if (tensix_harvesting_physical_layout & 1) {
            uint32_t const sorted_position =
                std::find(
                    sorted_harvesting_locations.begin(), sorted_harvesting_locations.end(), harvesting_locations[pos]) -
                sorted_harvesting_locations.begin();
            new_harvesting_mask |= (1 << sorted_position);
        }
        tensix_harvesting_physical_layout >>= 1;
        pos++;
    }

    return new_harvesting_mask;
}

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask_to_noc0_coords(
    tt::ARCH arch, uint32_t tensix_harvesting_logical_layout) {
    std::vector<uint32_t> sorted_harvesting_locations =
        architecture_implementation::create(arch)->get_harvesting_noc_locations();

    std::sort(sorted_harvesting_locations.begin(), sorted_harvesting_locations.end());
    size_t new_harvesting_mask = 0;
    uint32_t pos = 0;
    while (tensix_harvesting_logical_layout > 0) {
        if (tensix_harvesting_logical_layout & 1) {
            new_harvesting_mask |= (1 << sorted_harvesting_locations[pos]);
        }
        tensix_harvesting_logical_layout >>= 1;
        pos++;
    }

    return new_harvesting_mask;
}

uint32_t CoordinateManager::shuffle_l2cpu_harvesting_mask(tt::ARCH arch, uint32_t l2cpu_enabled_physical_layout) {
    if (arch != tt::ARCH::BLACKHOLE) {
        throw std::runtime_error("L2CPU cores currently only present in Blackhole.");
    }

    uint32_t harvesting_mask = 0;
    harvesting_mask |= (~l2cpu_enabled_physical_layout & 0x1) ? 1 << 0 : 0;  // 8, 3
    harvesting_mask |= (~l2cpu_enabled_physical_layout & 0x2) ? 1 << 3 : 0;  // 8, 9
    harvesting_mask |= (~l2cpu_enabled_physical_layout & 0x4) ? 1 << 1 : 0;  // 8, 5
    harvesting_mask |= (~l2cpu_enabled_physical_layout & 0x8) ? 1 << 2 : 0;  // 8, 7

    return harvesting_mask;
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
        default:
            throw std::runtime_error("Core type is not supported for getting noc0 pairs");
    }
}

std::vector<CoreCoord> CoordinateManager::get_all_noc0_cores(const CoreType core_type) const {
    const std::vector<tt_xy_pair>& noc0_pairs = get_noc0_pairs(core_type);
    std::vector<CoreCoord> noc0_cores;
    for (const tt_xy_pair& core : noc0_pairs) {
        CoreCoord const core_coord(core.x, core.y, core_type, CoordSystem::NOC0);
        noc0_cores.push_back(core_coord);
    }
    return noc0_cores;
}

std::vector<CoreCoord> CoordinateManager::get_tensix_cores() const {
    std::vector<size_t> harvested_y_coords = get_harvested_indices(harvesting_masks.tensix_harvesting_mask);
    std::vector<CoreCoord> unharvested_tensix_cores;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (std::find(harvested_y_coords.begin(), harvested_y_coords.end(), y) == harvested_y_coords.end()) {
            for (size_t x = 0; x < tensix_grid_size.x; x++) {
                const tt_xy_pair core = tensix_cores[y * tensix_grid_size.x + x];
                CoreCoord const core_coord(core.x, core.y, CoreType::TENSIX, CoordSystem::NOC0);

                unharvested_tensix_cores.push_back(core_coord);
            }
        }
    }
    return unharvested_tensix_cores;
}

std::vector<CoreCoord> CoordinateManager::get_harvested_tensix_cores() const {
    std::vector<size_t> harvested_y_coords = get_harvested_indices(harvesting_masks.tensix_harvesting_mask);
    std::vector<CoreCoord> harvested_tensix_cores;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (std::find(harvested_y_coords.begin(), harvested_y_coords.end(), y) != harvested_y_coords.end()) {
            for (size_t x = 0; x < tensix_grid_size.x; x++) {
                const tt_xy_pair core = tensix_cores[y * tensix_grid_size.x + x];
                CoreCoord const core_coord(core.x, core.y, CoreType::TENSIX, CoordSystem::NOC0);

                harvested_tensix_cores.push_back(core_coord);
            }
        }
    }
    return harvested_tensix_cores;
}

std::vector<CoreCoord> CoordinateManager::get_dram_cores() const { return get_all_noc0_cores(CoreType::DRAM); }

std::vector<CoreCoord> CoordinateManager::get_harvested_dram_cores() const { return {}; }

std::vector<CoreCoord> CoordinateManager::get_eth_cores() const { return get_all_noc0_cores(CoreType::ETH); }

std::vector<CoreCoord> CoordinateManager::get_harvested_eth_cores() const { return {}; }

std::vector<CoreCoord> CoordinateManager::get_pcie_cores() const { return get_all_noc0_cores(CoreType::PCIE); }

std::vector<CoreCoord> CoordinateManager::get_harvested_pcie_cores() const { return {}; }

std::vector<CoreCoord> CoordinateManager::get_l2cpu_cores() const { return get_all_noc0_cores(CoreType::L2CPU); }

std::vector<CoreCoord> CoordinateManager::get_harvested_l2cpu_cores() const { return {}; }

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
        case CoreType::ARC:
        case CoreType::ROUTER_ONLY:
        case CoreType::SECURITY:
            return get_all_noc0_cores(core_type);
        default:
            throw std::runtime_error("Core type is not supported for getting cores");
    }
}

tt_xy_pair CoordinateManager::get_dram_grid_size() const { return dram_grid_size; }

tt_xy_pair CoordinateManager::get_grid_size(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_tensix_grid_size();
        case CoreType::DRAM:
            return get_dram_grid_size();
        case CoreType::ARC:
            return arc_grid_size;
        case CoreType::PCIE:
            return pcie_grid_size;
        default:
            throw std::runtime_error("Core type is not supported for getting grid size");
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
        case CoreType::ARC:
        case CoreType::ROUTER_ONLY:
        case CoreType::SECURITY:
        case CoreType::L2CPU:
            return {};
        default:
            throw std::runtime_error("Core type is not supported for getting harvested cores");
    }
}

tt_xy_pair CoordinateManager::get_harvested_tensix_grid_size() const {
    return {tensix_grid_size.x, CoordinateManager::get_num_harvested(harvesting_masks.tensix_harvesting_mask)};
}

tt_xy_pair CoordinateManager::get_harvested_dram_grid_size() const { return {0, 0}; }

tt_xy_pair CoordinateManager::get_harvested_grid_size(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_harvested_tensix_grid_size();
        case CoreType::DRAM:
            return get_harvested_dram_grid_size();
        case CoreType::ARC:
        case CoreType::PCIE:
            return {0, 0};
        default:
            throw std::runtime_error("Core type is not supported for getting harvested grid size");
    }
}

uint32_t CoordinateManager::get_num_eth_channels() const {
    return num_eth_channels - CoordinateManager::get_num_harvested(harvesting_masks.eth_harvesting_mask);
}

uint32_t CoordinateManager::get_num_harvested_eth_channels() const {
    return CoordinateManager::get_num_harvested(harvesting_masks.eth_harvesting_mask);
}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch,
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
    const BoardType board_type,
    uint8_t asic_location) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return create_coordinate_manager(
                arch,
                noc_translation_enabled,
                harvesting_masks,
                wormhole::TENSIX_GRID_SIZE,
                wormhole::TENSIX_CORES_NOC0,
                wormhole::DRAM_GRID_SIZE,
                flatten_vector(wormhole::DRAM_CORES_NOC0),
                wormhole::ETH_CORES_NOC0,
                wormhole::ARC_GRID_SIZE,
                wormhole::ARC_CORES_NOC0,
                wormhole::PCIE_GRID_SIZE,
                wormhole::PCIE_CORES_NOC0,
                wormhole::ROUTER_CORES_NOC0,
                wormhole::SECURITY_CORES_NOC0,
                wormhole::L2CPU_CORES_NOC0,
                wormhole::NOC0_X_TO_NOC1_X,
                wormhole::NOC0_Y_TO_NOC1_Y);
        case tt::ARCH::QUASAR:  // TODO (#450): Add Quasar configuration
        case tt::ARCH::BLACKHOLE: {
            return create_coordinate_manager(
                arch,
                noc_translation_enabled,
                harvesting_masks,
                blackhole::TENSIX_GRID_SIZE,
                blackhole::TENSIX_CORES_NOC0,
                blackhole::DRAM_GRID_SIZE,
                flatten_vector(blackhole::DRAM_CORES_NOC0),
                blackhole::ETH_CORES_NOC0,
                blackhole::ARC_GRID_SIZE,
                blackhole::ARC_CORES_NOC0,
                blackhole::PCIE_GRID_SIZE,
                blackhole::PCIE_CORES_NOC0,
                blackhole::ROUTER_CORES_NOC0,
                blackhole::SECURITY_CORES_NOC0,
                blackhole::L2CPU_CORES_NOC0,
                blackhole::NOC0_X_TO_NOC1_X,
                blackhole::NOC0_Y_TO_NOC1_Y);
        }
        case tt::ARCH::Invalid:
            throw std::runtime_error("Invalid architecture for creating coordinate manager");
        default:
            throw std::runtime_error("Unexpected ARCH value " + std::to_string((int)arch));
    }
}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch,
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
    const std::vector<uint32_t>& noc0_y_to_noc1_y) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_shared<WormholeCoordinateManager>(
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
                noc0_y_to_noc1_y);
        case tt::ARCH::QUASAR:  // TODO (#450): Add Quasar configuration
        case tt::ARCH::BLACKHOLE:
            return std::make_shared<BlackholeCoordinateManager>(
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
                noc0_y_to_noc1_y);
        case tt::ARCH::Invalid:
            throw std::runtime_error("Invalid architecture for creating coordinate manager");
        default:
            throw std::runtime_error("Unexpected ARCH value " + std::to_string((int)arch));
    }
}

size_t CoordinateManager::get_num_harvested(const size_t harvesting_mask) {
    // Counts the number of 1 bits in harvesting mask, representing
    // the number of harvested tensix rows, tensix columns, DRAM banks...
    size_t num_harvested = 0;
    size_t mask = harvesting_mask;
    while (mask > 0) {
        num_harvested += mask & 1;
        mask >>= 1;
    }
    return num_harvested;
}

std::vector<size_t> CoordinateManager::get_harvested_indices(const size_t harvesting_mask) {
    std::vector<size_t> indices;
    size_t mask = harvesting_mask;
    size_t index = 0;
    while (mask > 0) {
        if (mask & 1) {
            indices.push_back(index);
        }
        mask >>= 1;
        index++;
    }

    return indices;
}

void CoordinateManager::add_noc1_to_noc0_mapping() {
    if (noc0_x_to_noc1_x.empty() || noc0_y_to_noc1_y.empty()) {
        return;
    }

    auto map_noc0_to_noc1_cores = [this](const std::vector<tt_xy_pair>& cores, CoreType core_type) {
        for (const tt_xy_pair& tensix_core : cores) {
            add_core_translation(
                CoreCoord(
                    noc0_x_to_noc1_x[tensix_core.x], noc0_y_to_noc1_y[tensix_core.y], core_type, CoordSystem::NOC1),
                tensix_core);
        }
    };

    map_noc0_to_noc1_cores(tensix_cores, CoreType::TENSIX);
    map_noc0_to_noc1_cores(dram_cores, CoreType::DRAM);
    map_noc0_to_noc1_cores(eth_cores, CoreType::ETH);
    map_noc0_to_noc1_cores(arc_cores, CoreType::ARC);
    map_noc0_to_noc1_cores(pcie_cores, CoreType::PCIE);
    map_noc0_to_noc1_cores(router_cores, CoreType::ROUTER_ONLY);
    map_noc0_to_noc1_cores(security_cores, CoreType::SECURITY);
    map_noc0_to_noc1_cores(l2cpu_cores, CoreType::L2CPU);
}

}  // namespace tt::umd
