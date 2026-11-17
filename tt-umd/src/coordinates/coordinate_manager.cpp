// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/coordinates/coordinate_manager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "coordinates/blackhole_coordinate_manager.hpp"
#include "coordinates/coordinate_manager_utils.hpp"
#include "coordinates/quasar_coordinate_manager.hpp"
#include "coordinates/wormhole_coordinate_manager.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/utils/common.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

using namespace coordinate_utils;

namespace {

// Every core type the manager keeps coordinates for.
constexpr std::array<CoreType, 9> CORE_TYPES = {
    CoreType::TENSIX,
    CoreType::DRAM,
    CoreType::ETH,
    CoreType::ARC,
    CoreType::PCIE,
    CoreType::ROUTER_ONLY,
    CoreType::SECURITY,
    CoreType::L2CPU,
    CoreType::DISPATCH};

std::vector<CoreCoord> to_noc0_core_coords(const std::vector<tt_xy_pair>& cores, CoreType core_type) {
    std::vector<CoreCoord> core_coords;
    core_coords.reserve(cores.size());
    for (const tt_xy_pair& core : cores) {
        core_coords.emplace_back(core, core_type, CoordSystem::NOC0);
    }
    return core_coords;
}

// Grid of a core type whose logical coordinates run along x, one core per column.
tt_xy_pair row_grid_size(size_t num_cores) { return num_cores == 0 ? tt_xy_pair(0, 0) : tt_xy_pair(num_cores, 1); }

}  // namespace

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
            UMD_THROW(error::RuntimeError, "No coordinate manager for architecture " + arch_to_str(arch) + ".");
    }
    // Translation runs through virtual functions, so it can only start once the derived object is complete.
    coordinate_manager->initialize();
    return coordinate_manager;
}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch,
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
    const BoardType board_type,
    const uint8_t asic_location) {
    // Which PCIe core a board uses is carried by the PCIe harvesting mask, so the board does not change the layout.
    const SocArchDescriptor arch_descriptor(arch);
    const std::vector<std::vector<tt_xy_pair>>& dram_cores = arch_descriptor.get_dram_cores();
    return create_coordinate_manager(
        arch,
        noc_translation_enabled,
        harvesting_masks,
        arch_descriptor.get_worker_grid_size(),
        arch_descriptor.get_tensix_cores(),
        {dram_cores.size(), dram_cores.empty() ? 0 : dram_cores.front().size()},
        flatten_vector(dram_cores),
        arch_descriptor.get_eth_cores(),
        SocArchDescriptor::calculate_grid_size(arch_descriptor.get_firmware_cores()),
        arch_descriptor.get_firmware_cores(),
        SocArchDescriptor::calculate_grid_size(arch_descriptor.get_pcie_cores()),
        arch_descriptor.get_pcie_cores(),
        arch_descriptor.get_router_cores(),
        arch_descriptor.get_security_cores(),
        arch_descriptor.get_l2cpu_cores(),
        arch_descriptor.get_dispatch_cores(),
        arch_descriptor.get_noc0_x_to_noc1_x(),
        arch_descriptor.get_noc0_y_to_noc1_y());
}

size_t CoordinateManager::get_num_harvested(const size_t harvesting_mask) {
    return static_cast<size_t>(__builtin_popcountll(harvesting_mask));
}

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH arch, uint32_t tensix_harvesting_physical_layout) {
    // LOGICAL_HARVESTING_LAYOUT[i] is the bit firmware reports the i-th Tensix row (Wormhole) or column (Blackhole)
    // in, counting rows or columns in NOC0 order.
    const std::vector<uint32_t>* harvesting_layout = nullptr;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            harvesting_layout = &wormhole::LOGICAL_HARVESTING_LAYOUT;
            break;
        case tt::ARCH::BLACKHOLE:
            harvesting_layout = &blackhole::LOGICAL_HARVESTING_LAYOUT;
            break;
        default:
            return tensix_harvesting_physical_layout;
    }

    uint32_t tensix_harvesting_noc0_layout = 0;
    for (size_t noc0_index = 0; noc0_index < harvesting_layout->size(); noc0_index++) {
        if (tensix_harvesting_physical_layout & (1u << (*harvesting_layout)[noc0_index])) {
            tensix_harvesting_noc0_layout |= 1u << noc0_index;
        }
    }
    return tensix_harvesting_noc0_layout;
}

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask_to_noc0_coords(
    tt::ARCH arch, uint32_t tensix_harvesting_logical_layout) {
    // Bit i of the result is NOC0 row (Wormhole) or column (Blackhole) i, rather than the i-th Tensix one.
    const std::vector<uint32_t>* tensix_locations = nullptr;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            tensix_locations = &wormhole::T6_Y_LOCATIONS;
            break;
        case tt::ARCH::BLACKHOLE:
            tensix_locations = &blackhole::T6_X_LOCATIONS;
            break;
        default:
            return tensix_harvesting_logical_layout;
    }

    uint32_t tensix_harvesting_noc0_coords = 0;
    for (size_t index : get_harvested_indices(tensix_harvesting_logical_layout)) {
        UMD_ASSERT(
            index < tensix_locations->size(),
            error::RuntimeError,
            "Tensix harvesting mask names a row or column the chip does not have.");
        tensix_harvesting_noc0_coords |= 1u << (*tensix_locations)[index];
    }
    return tensix_harvesting_noc0_coords;
}

uint32_t CoordinateManager::shuffle_l2cpu_harvesting_mask(tt::ARCH arch, uint32_t l2cpu_enabled_physical_layout) {
    if (arch != tt::ARCH::BLACKHOLE) {
        return 0;
    }
    // NOC0-ordered index (blackhole::L2CPU_CORES_NOC0) of each L2CPU in firmware's numbering: L2CPU 0..3 sit at
    // NOC0 y = 3, 9, 5, 7.
    constexpr std::array<uint32_t, 4> l2cpu_noc0_index = {0, 3, 1, 2};
    uint32_t l2cpu_harvesting_noc0_layout = 0;
    for (size_t physical_index = 0; physical_index < l2cpu_noc0_index.size(); physical_index++) {
        if (!(l2cpu_enabled_physical_layout & (1u << physical_index))) {
            l2cpu_harvesting_noc0_layout |= 1u << l2cpu_noc0_index[physical_index];
        }
    }
    return l2cpu_harvesting_noc0_layout;
}

CoreCoord CoordinateManager::translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const {
    const auto noc0_it = to_noc0_map.find(core_coord);
    UMD_ASSERT(noc0_it != to_noc0_map.end(), error::RuntimeError, "No core at " + core_coord.str() + ".");

    const auto target_it = from_noc0_map.find({noc0_it->second, coord_system});
    UMD_ASSERT(
        target_it != from_noc0_map.end(),
        error::RuntimeError,
        "Core " + core_coord.str() + " has no " + to_str(coord_system) + " coordinates.");
    return target_it->second;
}

CoreCoord CoordinateManager::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    // LOGICAL coordinates are not unique across core types, so they cannot name a core on their own.
    const auto it = to_core_type_map.find({core, coord_system});
    UMD_ASSERT(
        it != to_core_type_map.end(),
        error::RuntimeError,
        "No core at " + core.str() + " in " + to_str(coord_system) + " coordinates.");
    return it->second;
}

CoreCoord CoordinateManager::translate_coord_to(
    const tt_xy_pair core, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    return translate_coord_to(get_coord_at(core, input_coord_system), target_coord_system);
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
            UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " is not supported.");
    }
}

tt_xy_pair CoordinateManager::get_grid_size(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_tensix_grid_size();
        case CoreType::DRAM:
            return get_dram_grid_size();
        case CoreType::ETH: {
            const size_t num_eth_cores = get_eth_cores().size();
            return num_eth_cores == 0 ? tt_xy_pair(0, 0) : tt_xy_pair(1, num_eth_cores);
        }
        case CoreType::ARC:
            return arc_grid_size;
        case CoreType::PCIE:
        case CoreType::L2CPU:
        case CoreType::SECURITY:
        case CoreType::DISPATCH:
            return row_grid_size(get_cores(core_type).size());
        default:
            UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " is not laid out in a grid.");
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
            UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " is not supported.");
    }
}

tt_xy_pair CoordinateManager::get_harvested_grid_size(const CoreType core_type) const {
    switch (core_type) {
        case CoreType::TENSIX:
            return get_harvested_tensix_grid_size();
        case CoreType::DRAM:
            return get_harvested_dram_grid_size();
        case CoreType::ETH: {
            const size_t num_harvested_eth_cores = get_harvested_eth_cores().size();
            return num_harvested_eth_cores == 0 ? tt_xy_pair(0, 0) : tt_xy_pair(1, num_harvested_eth_cores);
        }
        case CoreType::ARC:
        case CoreType::SECURITY:
            return {0, 0};
        case CoreType::PCIE:
        case CoreType::L2CPU:
        case CoreType::DISPATCH:
            return row_grid_size(get_harvested_cores(core_type).size());
        default:
            UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " is not laid out in a grid.");
    }
}

HarvestingMasks CoordinateManager::get_harvesting_masks() const { return harvesting_masks; }

uint32_t CoordinateManager::get_num_eth_channels() const { return num_eth_channels; }

uint32_t CoordinateManager::get_num_harvested_eth_channels() const {
    return count_harvested(harvesting_masks.eth_harvesting_mask, eth_cores.size());
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
            UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " is not supported.");
    }
}

std::vector<CoreCoord> CoordinateManager::get_all_noc0_cores(const CoreType core_type) const {
    return to_noc0_core_coords(get_noc0_pairs(core_type), core_type);
}

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

void CoordinateManager::initialize() {
    assert_coordinate_manager_constructor();
    identity_map_noc0_cores();
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

void CoordinateManager::assert_coordinate_manager_constructor() {
    // Tensix and DRAM cores are indexed as full grids, row by row and bank by bank.
    UMD_ASSERT(
        tensix_cores.size() == tensix_grid_size.x * tensix_grid_size.y,
        error::RuntimeError,
        "Tensix cores do not fill the " + tensix_grid_size.str() + " Tensix grid.");
    UMD_ASSERT(
        dram_cores.size() == dram_grid_size.x * dram_grid_size.y,
        error::RuntimeError,
        "DRAM cores do not fill the " + dram_grid_size.str() + " DRAM grid.");
}

void CoordinateManager::translate_tensix_coords() {
    // Harvesting removes whole rows.
    UMD_ASSERT(
        get_num_harvested(harvesting_masks.tensix_harvesting_mask) <= tensix_grid_size.y,
        error::RuntimeError,
        "Tensix harvesting mask names more rows than the grid has.");
    size_t logical_y = 0;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (is_harvested(harvesting_masks.tensix_harvesting_mask, y)) {
            continue;
        }
        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            add_core_translation(
                CoreCoord(x, logical_y, CoreType::TENSIX, CoordSystem::LOGICAL),
                tensix_cores[y * tensix_grid_size.x + x]);
        }
        logical_y++;
    }
    fill_tensix_noc0_translated_mapping();
}

void CoordinateManager::translate_dram_coords() {
    // Harvesting removes whole banks; logical x is the bank among the working ones, logical y the NOC port.
    size_t logical_x = 0;
    for (size_t bank = 0; bank < dram_grid_size.x; bank++) {
        if (is_harvested(harvesting_masks.dram_harvesting_mask, bank)) {
            continue;
        }
        for (size_t port = 0; port < dram_grid_size.y; port++) {
            add_core_translation(
                CoreCoord(logical_x, port, CoreType::DRAM, CoordSystem::LOGICAL),
                dram_cores[bank * dram_grid_size.y + port]);
        }
        logical_x++;
    }
    fill_dram_noc0_translated_mapping();
}

void CoordinateManager::translate_eth_coords() {
    // Logical y is the channel among the working ones.
    const std::vector<tt_xy_pair> unharvested =
        filter_cores_by_mask(eth_cores, harvesting_masks.eth_harvesting_mask, false);
    for (size_t channel = 0; channel < unharvested.size(); channel++) {
        add_core_translation(CoreCoord(0, channel, CoreType::ETH, CoordSystem::LOGICAL), unharvested[channel]);
    }
    fill_eth_noc0_translated_mapping();
}

void CoordinateManager::translate_arc_coords() {
    for (size_t index = 0; index < arc_cores.size(); index++) {
        add_core_translation(
            CoreCoord(index % arc_grid_size.x, index / arc_grid_size.x, CoreType::ARC, CoordSystem::LOGICAL),
            arc_cores[index]);
    }
    fill_arc_noc0_translated_mapping();
}

void CoordinateManager::translate_pcie_coords() {
    const std::vector<tt_xy_pair> unharvested =
        filter_cores_by_mask(pcie_cores, harvesting_masks.pcie_harvesting_mask, false);
    for (const auto& [logical_coord, noc0_pair] : row_logical_translations(unharvested, CoreType::PCIE)) {
        add_core_translation(logical_coord, noc0_pair);
    }
    fill_pcie_noc0_translated_mapping();
}

void CoordinateManager::translate_router_coords() {
    // Routers have no logical coordinates and are never translated.
    for (const auto& [translated_coord, noc0_pair] : identity_translations(router_cores, CoreType::ROUTER_ONLY)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::translate_security_coords() {
    for (const auto& [logical_coord, noc0_pair] : row_logical_translations(security_cores, CoreType::SECURITY)) {
        add_core_translation(logical_coord, noc0_pair);
    }
    for (const auto& [translated_coord, noc0_pair] : identity_translations(security_cores, CoreType::SECURITY)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::translate_l2cpu_coords() {
    const std::vector<tt_xy_pair> unharvested =
        filter_cores_by_mask(l2cpu_cores, harvesting_masks.l2cpu_harvesting_mask, false);
    for (const auto& [logical_coord, noc0_pair] : row_logical_translations(unharvested, CoreType::L2CPU)) {
        add_core_translation(logical_coord, noc0_pair);
    }
    for (const auto& [translated_coord, noc0_pair] : identity_translations(l2cpu_cores, CoreType::L2CPU)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::translate_dispatch_coords() {
    for (const auto& [logical_coord, noc0_pair] : row_logical_translations(dispatch_cores, CoreType::DISPATCH)) {
        add_core_translation(logical_coord, noc0_pair);
    }
    for (const auto& [translated_coord, noc0_pair] : identity_translations(dispatch_cores, CoreType::DISPATCH)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::identity_map_noc0_cores() {
    for (const CoreType core_type : CORE_TYPES) {
        for (const tt_xy_pair& core : get_noc0_pairs(core_type)) {
            add_core_translation(CoreCoord(core, core_type, CoordSystem::NOC0), core);
        }
    }
}

void CoordinateManager::add_core_translation(const CoreCoord& core_coord, const tt_xy_pair& noc0_pair) {
    to_noc0_map[core_coord] = noc0_pair;
    from_noc0_map[{noc0_pair, core_coord.coord_system}] = core_coord;

    if (core_coord.coord_system == CoordSystem::LOGICAL) {
        return;
    }
    // Outside LOGICAL a location names one core, which is what lets get_coord_at resolve the core type.
    const auto [it, inserted] =
        to_core_type_map.try_emplace({core_coord.to_pair(), core_coord.coord_system}, core_coord);
    UMD_ASSERT(
        inserted || it->second == core_coord,
        error::RuntimeError,
        "Cores " + it->second.str() + " and " + core_coord.str() + " share a location.");
}

void CoordinateManager::add_noc1_to_noc0_mapping() {
    if (noc0_x_to_noc1_x.empty() || noc0_y_to_noc1_y.empty()) {
        log_debug(tt::LogUMD, "No NOC0 to NOC1 mapping given, NOC1 coordinates are unavailable.");
        return;
    }
    for (const CoreType core_type : CORE_TYPES) {
        for (const tt_xy_pair& core : get_noc0_pairs(core_type)) {
            UMD_ASSERT(
                core.x < noc0_x_to_noc1_x.size() && core.y < noc0_y_to_noc1_y.size(),
                error::RuntimeError,
                "NOC0 to NOC1 mapping does not cover core " + core.str() + ".");
            add_core_translation(
                CoreCoord(noc0_x_to_noc1_x[core.x], noc0_y_to_noc1_y[core.y], core_type, CoordSystem::NOC1), core);
        }
    }
}

std::vector<CoreCoord> CoordinateManager::get_tensix_cores() const {
    std::vector<CoreCoord> cores;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (is_harvested(harvesting_masks.tensix_harvesting_mask, y)) {
            continue;
        }
        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            cores.emplace_back(tensix_cores[y * tensix_grid_size.x + x], CoreType::TENSIX, CoordSystem::NOC0);
        }
    }
    return cores;
}

std::vector<CoreCoord> CoordinateManager::get_harvested_tensix_cores() const {
    std::vector<CoreCoord> cores;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (!is_harvested(harvesting_masks.tensix_harvesting_mask, y)) {
            continue;
        }
        for (size_t x = 0; x < tensix_grid_size.x; x++) {
            cores.emplace_back(tensix_cores[y * tensix_grid_size.x + x], CoreType::TENSIX, CoordSystem::NOC0);
        }
    }
    return cores;
}

std::vector<CoreCoord> CoordinateManager::get_dram_cores() const {
    std::vector<CoreCoord> cores;
    for (size_t bank = 0; bank < dram_grid_size.x; bank++) {
        if (is_harvested(harvesting_masks.dram_harvesting_mask, bank)) {
            continue;
        }
        for (size_t port = 0; port < dram_grid_size.y; port++) {
            cores.emplace_back(dram_cores[bank * dram_grid_size.y + port], CoreType::DRAM, CoordSystem::NOC0);
        }
    }
    return cores;
}

std::vector<CoreCoord> CoordinateManager::get_harvested_dram_cores() const {
    std::vector<CoreCoord> cores;
    for (size_t bank = 0; bank < dram_grid_size.x; bank++) {
        if (!is_harvested(harvesting_masks.dram_harvesting_mask, bank)) {
            continue;
        }
        for (size_t port = 0; port < dram_grid_size.y; port++) {
            cores.emplace_back(dram_cores[bank * dram_grid_size.y + port], CoreType::DRAM, CoordSystem::NOC0);
        }
    }
    return cores;
}

std::vector<CoreCoord> CoordinateManager::get_eth_cores() const {
    return to_noc0_core_coords(
        filter_cores_by_mask(eth_cores, harvesting_masks.eth_harvesting_mask, false), CoreType::ETH);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_eth_cores() const {
    return to_noc0_core_coords(
        filter_cores_by_mask(eth_cores, harvesting_masks.eth_harvesting_mask, true), CoreType::ETH);
}

std::vector<CoreCoord> CoordinateManager::get_pcie_cores() const {
    return to_noc0_core_coords(
        filter_cores_by_mask(pcie_cores, harvesting_masks.pcie_harvesting_mask, false), CoreType::PCIE);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_pcie_cores() const {
    return to_noc0_core_coords(
        filter_cores_by_mask(pcie_cores, harvesting_masks.pcie_harvesting_mask, true), CoreType::PCIE);
}

std::vector<CoreCoord> CoordinateManager::get_l2cpu_cores() const {
    return to_noc0_core_coords(
        filter_cores_by_mask(l2cpu_cores, harvesting_masks.l2cpu_harvesting_mask, false), CoreType::L2CPU);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_l2cpu_cores() const {
    return to_noc0_core_coords(
        filter_cores_by_mask(l2cpu_cores, harvesting_masks.l2cpu_harvesting_mask, true), CoreType::L2CPU);
}

std::vector<CoreCoord> CoordinateManager::get_dispatch_cores() const {
    return to_noc0_core_coords(dispatch_cores, CoreType::DISPATCH);
}

std::vector<CoreCoord> CoordinateManager::get_harvested_dispatch_cores() const { return {}; }

tt_xy_pair CoordinateManager::get_dram_grid_size() const {
    return {
        dram_grid_size.x - count_harvested(harvesting_masks.dram_harvesting_mask, dram_grid_size.x), dram_grid_size.y};
}

tt_xy_pair CoordinateManager::get_harvested_tensix_grid_size() const {
    const size_t num_harvested_rows = count_harvested(harvesting_masks.tensix_harvesting_mask, tensix_grid_size.y);
    return num_harvested_rows == 0 ? tt_xy_pair(0, 0) : tt_xy_pair(tensix_grid_size.x, num_harvested_rows);
}

tt_xy_pair CoordinateManager::get_harvested_dram_grid_size() const {
    const size_t num_harvested_banks = count_harvested(harvesting_masks.dram_harvesting_mask, dram_grid_size.x);
    return num_harvested_banks == 0 ? tt_xy_pair(0, 0) : tt_xy_pair(num_harvested_banks, dram_grid_size.y);
}

void CoordinateManager::fill_tensix_default_noc0_translated_mapping() {
    for (const auto& [translated_coord, noc0_pair] : identity_translations(tensix_cores, CoreType::TENSIX)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::fill_eth_default_noc0_translated_mapping() {
    for (const auto& [translated_coord, noc0_pair] : identity_translations(eth_cores, CoreType::ETH)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::fill_dram_default_noc0_translated_mapping() {
    for (const auto& [translated_coord, noc0_pair] : identity_translations(dram_cores, CoreType::DRAM)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::fill_pcie_default_noc0_translated_mapping() {
    for (const auto& [translated_coord, noc0_pair] : identity_translations(pcie_cores, CoreType::PCIE)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

void CoordinateManager::fill_arc_default_noc0_translated_mapping() {
    for (const auto& [translated_coord, noc0_pair] : identity_translations(arc_cores, CoreType::ARC)) {
        add_core_translation(translated_coord, noc0_pair);
    }
}

std::vector<size_t> CoordinateManager::get_harvested_indices(const size_t harvesting_mask) {
    std::vector<size_t> indices;
    for (size_t index = 0; index < sizeof(harvesting_mask) * 8; index++) {
        if (is_harvested(harvesting_mask, index)) {
            indices.push_back(index);
        }
    }
    return indices;
}

}  // namespace tt::umd
