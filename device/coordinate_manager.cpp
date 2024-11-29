/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/coordinate_manager.h"

#include "umd/device/blackhole_coordinate_manager.h"
#include "umd/device/grayskull_coordinate_manager.h"
#include "umd/device/wormhole_coordinate_manager.h"

using namespace tt::umd;

CoordinateManager::CoordinateManager(
    const tt_xy_pair& tensix_grid_size,
    const std::vector<tt_xy_pair>& tensix_cores,
    const size_t tensix_harvesting_mask,
    const tt_xy_pair& dram_grid_size,
    const std::vector<tt_xy_pair>& dram_cores,
    const size_t dram_harvesting_mask,
    const tt_xy_pair& eth_grid_size,
    const std::vector<tt_xy_pair>& eth_cores,
    const tt_xy_pair& arc_grid_size,
    const std::vector<tt_xy_pair>& arc_cores,
    const tt_xy_pair& pcie_grid_size,
    const std::vector<tt_xy_pair>& pcie_cores) :
    tensix_grid_size(tensix_grid_size),
    tensix_cores(tensix_cores),
    tensix_harvesting_mask(tensix_harvesting_mask),
    dram_grid_size(dram_grid_size),
    dram_cores(dram_cores),
    dram_harvesting_mask(dram_harvesting_mask),
    eth_grid_size(eth_grid_size),
    eth_cores(eth_cores),
    arc_grid_size(arc_grid_size),
    arc_cores(arc_cores),
    pcie_grid_size(pcie_grid_size),
    pcie_cores(pcie_cores) {}

std::map<tt_xy_pair, CoreCoord>& CoordinateManager::get_logical_to_translated(CoreType core_type) {
    switch (core_type) {
        case CoreType::TENSIX:
            return tensix_logical_to_translated;
        case CoreType::DRAM:
            return dram_logical_to_translated;
        case CoreType::ACTIVE_ETH:
        case CoreType::IDLE_ETH:
        case CoreType::ETH:
            return eth_logical_to_translated;
        case CoreType::ARC:
            return arc_logical_to_translated;
        case CoreType::PCIE:
            return pcie_logical_to_translated;
        default:
            throw std::runtime_error("Core type is not supported for getting logical to translated mapping");
    }
}

std::map<tt_xy_pair, CoreCoord>& CoordinateManager::get_logical_to_virtual(CoreType core_type) {
    switch (core_type) {
        case CoreType::TENSIX:
            return tensix_logical_to_virtual;
        case CoreType::DRAM:
            return dram_logical_to_virtual;
        case CoreType::ACTIVE_ETH:
        case CoreType::IDLE_ETH:
        case CoreType::ETH:
            return eth_logical_to_virtual;
        case CoreType::ARC:
            return arc_logical_to_virtual;
        case CoreType::PCIE:
            return pcie_logical_to_virtual;
        default:
            throw std::runtime_error("Core type is not supported for getting logical to virtual mapping");
    }
}

std::map<tt_xy_pair, CoreCoord>& CoordinateManager::get_logical_to_physical(CoreType core_type) {
    switch (core_type) {
        case CoreType::TENSIX:
            return tensix_logical_to_physical;
        case CoreType::DRAM:
            return dram_logical_to_physical;
        case CoreType::ACTIVE_ETH:
        case CoreType::IDLE_ETH:
        case CoreType::ETH:
            return eth_logical_to_physical;
        case CoreType::ARC:
            return arc_logical_to_physical;
        case CoreType::PCIE:
            return pcie_logical_to_physical;
        default:
            throw std::runtime_error("Core type is not supported for getting logical to physical mapping");
    }
}

std::map<tt_xy_pair, CoreCoord>& CoordinateManager::get_physical_to_logical(CoreType core_type) {
    switch (core_type) {
        case CoreType::TENSIX:
            return tensix_physical_to_logical;
        case CoreType::DRAM:
            return dram_physical_to_logical;
        case CoreType::ACTIVE_ETH:
        case CoreType::IDLE_ETH:
        case CoreType::ETH:
            return eth_physical_to_logical;
        case CoreType::ARC:
            return arc_physical_to_logical;
        case CoreType::PCIE:
            return pcie_physical_to_logical;
        default:
            throw std::runtime_error("Core type is not supported for getting physical to logical mapping");
    }
}

std::map<tt_xy_pair, CoreCoord>& CoordinateManager::get_virtual_to_logical(CoreType core_type) {
    switch (core_type) {
        case CoreType::TENSIX:
            return tensix_virtual_to_logical;
        case CoreType::DRAM:
            return dram_virtual_to_logical;
        case CoreType::ACTIVE_ETH:
        case CoreType::IDLE_ETH:
        case CoreType::ETH:
            return eth_virtual_to_logical;
        case CoreType::ARC:
            return arc_virtual_to_logical;
        case CoreType::PCIE:
            return pcie_virtual_to_logical;
        default:
            throw std::runtime_error("Core type is not supported for getting virtual to logical mapping");
    }
}

std::map<tt_xy_pair, CoreCoord>& CoordinateManager::get_translated_to_logical(CoreType core_type) {
    switch (core_type) {
        case CoreType::TENSIX:
            return tensix_translated_to_logical;
        case CoreType::DRAM:
            return dram_translated_to_logical;
        case CoreType::ACTIVE_ETH:
        case CoreType::IDLE_ETH:
        case CoreType::ETH:
            return eth_translated_to_logical;
        case CoreType::ARC:
            return arc_translated_to_logical;
        case CoreType::PCIE:
            return pcie_translated_to_logical;
        default:
            throw std::runtime_error("Core type is not supported for getting translated to logical mapping");
    }
}

CoreCoord CoordinateManager::to_physical(const CoreCoord core_coord) {
    switch (core_coord.coord_system) {
        case CoordSystem::PHYSICAL:
            return core_coord;
        case CoordSystem::VIRTUAL:
        case CoordSystem::TRANSLATED:
            return to_physical(to_logical(core_coord));
    }

    // Coord system is surely logical.
    auto& logical_mapping = get_logical_to_physical(core_coord.core_type);
    return logical_mapping[{core_coord.x, core_coord.y}];
}

CoreCoord CoordinateManager::to_virtual(const CoreCoord core_coord) {
    switch (core_coord.coord_system) {
        case CoordSystem::TRANSLATED:
        case CoordSystem::PHYSICAL:
            return to_virtual(to_logical(core_coord));
        case CoordSystem::VIRTUAL:
            return core_coord;
    }

    // Coord system is surely logical.
    auto& logical_mapping = get_logical_to_virtual(core_coord.core_type);
    return logical_mapping[{core_coord.x, core_coord.y}];
}

CoreCoord CoordinateManager::to_logical(const CoreCoord core_coord) {
    switch (core_coord.coord_system) {
        case CoordSystem::LOGICAL:
            return core_coord;
        case CoordSystem::PHYSICAL: {
            auto& physical_mapping = get_physical_to_logical(core_coord.core_type);
            return physical_mapping[{core_coord.x, core_coord.y}];
        }
        case CoordSystem::VIRTUAL: {
            auto& virtual_mapping = get_virtual_to_logical(core_coord.core_type);
            return virtual_mapping[{core_coord.x, core_coord.y}];
        }
        case CoordSystem::TRANSLATED: {
            auto& translated_mapping = get_translated_to_logical(core_coord.core_type);
            return translated_mapping[{core_coord.x, core_coord.y}];
        }
    }
}

CoreCoord CoordinateManager::to_translated(const CoreCoord core_coord) {
    switch (core_coord.coord_system) {
        case CoordSystem::PHYSICAL:
        case CoordSystem::VIRTUAL:
            return to_translated(to_logical(core_coord));
        case CoordSystem::TRANSLATED:
            return core_coord;
    }

    // Coord system is surely logical.
    auto& logical_mapping = get_logical_to_translated(core_coord.core_type);
    return logical_mapping[{core_coord.x, core_coord.y}];
}

CoreCoord CoordinateManager::to(const CoreCoord core_coord, const CoordSystem coord_system) {
    switch (coord_system) {
        case CoordSystem::LOGICAL:
            return to_logical(core_coord);
        case CoordSystem::PHYSICAL:
            return to_physical(core_coord);
        case CoordSystem::VIRTUAL:
            return to_virtual(core_coord);
        case CoordSystem::TRANSLATED:
            return to_translated(core_coord);
    }
}

void CoordinateManager::translate_tensix_coords() {
    size_t num_harvested_y = __builtin_popcount(tensix_harvesting_mask);
    size_t grid_size_x = tensix_grid_size.x;
    size_t grid_size_y = tensix_grid_size.y;

    size_t logical_y = 0;
    for (size_t y = 0; y < grid_size_y; y++) {
        if (!(tensix_harvesting_mask & (1 << y))) {
            for (size_t x = 0; x < grid_size_x; x++) {
                const tt_xy_pair& tensix_core = tensix_cores[y * grid_size_x + x];
                tensix_logical_to_physical[{x, logical_y}] =
                    CoreCoord(tensix_core.x, tensix_core.y, CoreType::TENSIX, CoordSystem::PHYSICAL);
                tensix_physical_to_logical[tensix_core] =
                    CoreCoord(x, logical_y, CoreType::TENSIX, CoordSystem::LOGICAL);
            }
            logical_y++;
        }
    }

    for (size_t y = 0; y < grid_size_y - num_harvested_y; y++) {
        for (size_t x = 0; x < grid_size_x; x++) {
            const tt_xy_pair& tensix_core = tensix_cores[y * grid_size_x + x];
            tensix_logical_to_virtual[{x, y}] =
                CoreCoord(tensix_core.x, tensix_core.y, CoreType::TENSIX, CoordSystem::VIRTUAL);
            tensix_virtual_to_logical[tensix_core] = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
        }
    }

    fill_tensix_logical_to_translated();
}

void CoordinateManager::fill_tensix_logical_to_translated() {
    size_t num_harvested_y = __builtin_popcount(tensix_harvesting_mask);

    for (size_t x = 0; x < tensix_grid_size.x; x++) {
        for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
            const CoreCoord physical_coord = tensix_logical_to_physical[{x, y}];
            const size_t translated_x = physical_coord.x;
            const size_t translated_y = physical_coord.y;
            tensix_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);
            tensix_translated_to_logical[tt_xy_pair(translated_x, translated_y)] =
                CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
        }
    }
}

void CoordinateManager::translate_dram_coords() {
    for (size_t x = 0; x < dram_grid_size.x; x++) {
        for (size_t y = 0; y < dram_grid_size.y; y++) {
            const tt_xy_pair dram_core = dram_cores[x * dram_grid_size.y + y];
            dram_logical_to_virtual[{x, y}] = CoreCoord(dram_core.x, dram_core.y, CoreType::DRAM, CoordSystem::VIRTUAL);
            dram_virtual_to_logical[dram_core] = CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);

            dram_logical_to_physical[{x, y}] =
                CoreCoord(dram_core.x, dram_core.y, CoreType::DRAM, CoordSystem::PHYSICAL);
            dram_physical_to_logical[dram_core] = CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);
        }
    }

    fill_dram_logical_to_translated();
}

void CoordinateManager::translate_eth_coords() {
    for (size_t x = 0; x < eth_grid_size.x; x++) {
        for (size_t y = 0; y < eth_grid_size.y; y++) {
            const tt_xy_pair eth_core = eth_cores[x * eth_grid_size.y + y];
            eth_logical_to_virtual[{x, y}] = CoreCoord(eth_core.x, eth_core.y, CoreType::ETH, CoordSystem::VIRTUAL);
            eth_virtual_to_logical[eth_core] = CoreCoord(x, y, CoreType::ETH, CoordSystem::LOGICAL);

            eth_logical_to_physical[{x, y}] = CoreCoord(eth_core.x, eth_core.y, CoreType::ETH, CoordSystem::PHYSICAL);
            eth_physical_to_logical[eth_core] = CoreCoord(x, y, CoreType::ETH, CoordSystem::LOGICAL);
        }
    }

    fill_eth_logical_to_translated();
}

void CoordinateManager::translate_arc_coords() {
    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            const tt_xy_pair arc_core = arc_cores[x * arc_grid_size.y + y];
            arc_logical_to_virtual[{x, y}] = CoreCoord(arc_core.x, arc_core.y, CoreType::ARC, CoordSystem::VIRTUAL);
            arc_virtual_to_logical[arc_core] = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);

            arc_logical_to_physical[{x, y}] = CoreCoord(arc_core.x, arc_core.y, CoreType::ARC, CoordSystem::PHYSICAL);
            arc_physical_to_logical[arc_core] = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);

            arc_logical_to_translated[{x, y}] =
                CoreCoord(arc_core.x, arc_core.y, CoreType::ARC, CoordSystem::TRANSLATED);
            arc_translated_to_logical[arc_core] = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);
        }
    }

    fill_arc_logical_to_translated();
}

void CoordinateManager::translate_pcie_coords() {
    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            const tt_xy_pair pcie_core = pcie_cores[x * pcie_grid_size.y + y];
            pcie_logical_to_virtual[{x, y}] = CoreCoord(pcie_core.x, pcie_core.y, CoreType::PCIE, CoordSystem::VIRTUAL);
            pcie_virtual_to_logical[pcie_core] = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);

            pcie_logical_to_physical[{x, y}] =
                CoreCoord(pcie_core.x, pcie_core.y, CoreType::PCIE, CoordSystem::PHYSICAL);
            pcie_physical_to_logical[pcie_core] = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);
        }
    }

    fill_pcie_logical_to_translated();
}

void CoordinateManager::fill_eth_logical_to_translated() {
    for (size_t x = 0; x < eth_grid_size.x; x++) {
        for (size_t y = 0; y < eth_grid_size.y; y++) {
            const CoreCoord physical_coord = eth_logical_to_physical[{x, y}];
            const size_t translated_x = physical_coord.x;
            const size_t translated_y = physical_coord.y;
            eth_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::ETH, CoordSystem::TRANSLATED);
            eth_translated_to_logical[{translated_x, translated_y}] =
                CoreCoord(x, y, CoreType::ETH, CoordSystem::LOGICAL);
        }
    }
}

void CoordinateManager::fill_dram_logical_to_translated() {
    for (size_t x = 0; x < dram_grid_size.x; x++) {
        for (size_t y = 0; y < dram_grid_size.y; y++) {
            const CoreCoord physical_coord = dram_logical_to_physical[{x, y}];
            const size_t translated_x = physical_coord.x;
            const size_t translated_y = physical_coord.y;
            dram_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::DRAM, CoordSystem::TRANSLATED);
            dram_translated_to_logical[{translated_x, translated_y}] =
                CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);
        }
    }
}

void CoordinateManager::fill_pcie_logical_to_translated() {
    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            const CoreCoord physical_coord = pcie_logical_to_physical[{x, y}];
            const size_t translated_x = physical_coord.x;
            const size_t translated_y = physical_coord.y;
            pcie_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::PCIE, CoordSystem::TRANSLATED);
            pcie_translated_to_logical[{translated_x, translated_y}] =
                CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);
        }
    }
}

void CoordinateManager::fill_arc_logical_to_translated() {
    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            const CoreCoord physical_coord = arc_logical_to_physical[{x, y}];
            const size_t translated_x = physical_coord.x;
            const size_t translated_y = physical_coord.y;
            arc_logical_to_translated[{x, y}] =
                CoreCoord(translated_x, translated_y, CoreType::ARC, CoordSystem::TRANSLATED);
            arc_translated_to_logical[{translated_x, translated_y}] =
                CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);
        }
    }
}

std::shared_ptr<CoordinateManager> CoordinateManager::get_coordinate_manager(
    tt::ARCH arch, const size_t tensix_harvesting_mask, const size_t dram_harvesting_mask) {
    switch (arch) {
        case tt::ARCH::GRAYSKULL:
            return get_coordinate_manager(
                arch,
                tt::umd::grayskull::TENSIX_GRID_SIZE,
                tt::umd::grayskull::TENSIX_CORES,
                tensix_harvesting_mask,
                tt::umd::grayskull::DRAM_GRID_SIZE,
                tt::umd::grayskull::DRAM_CORES,
                dram_harvesting_mask,
                tt::umd::grayskull::ETH_GRID_SIZE,
                tt::umd::grayskull::ETH_CORES,
                tt::umd::grayskull::ARC_GRID_SIZE,
                tt::umd::grayskull::ARC_CORES,
                tt::umd::grayskull::PCIE_GRID_SIZE,
                tt::umd::grayskull::PCIE_CORES);
        case tt::ARCH::WORMHOLE_B0:
            return get_coordinate_manager(
                arch,
                tt::umd::wormhole::TENSIX_GRID_SIZE,
                tt::umd::wormhole::TENSIX_CORES,
                tensix_harvesting_mask,
                tt::umd::wormhole::DRAM_GRID_SIZE,
                tt::umd::wormhole::DRAM_CORES,
                dram_harvesting_mask,
                tt::umd::wormhole::ETH_GRID_SIZE,
                tt::umd::wormhole::ETH_CORES,
                tt::umd::wormhole::ARC_GRID_SIZE,
                tt::umd::wormhole::ARC_CORES,
                tt::umd::wormhole::PCIE_GRID_SIZE,
                tt::umd::wormhole::PCIE_CORES);
        case tt::ARCH::BLACKHOLE:
            return get_coordinate_manager(
                arch,
                tt::umd::blackhole::TENSIX_GRID_SIZE,
                tt::umd::blackhole::TENSIX_CORES,
                tensix_harvesting_mask,
                tt::umd::blackhole::DRAM_GRID_SIZE,
                tt::umd::blackhole::DRAM_CORES,
                dram_harvesting_mask,
                tt::umd::blackhole::ETH_GRID_SIZE,
                tt::umd::blackhole::ETH_CORES,
                tt::umd::blackhole::ARC_GRID_SIZE,
                tt::umd::blackhole::ARC_CORES,
                tt::umd::blackhole::PCIE_GRID_SIZE,
                tt::umd::blackhole::PCIE_CORES);
    }
}

std::shared_ptr<CoordinateManager> CoordinateManager::get_coordinate_manager(
    tt::ARCH arch,
    const tt_xy_pair& tensix_grid_size,
    const std::vector<tt_xy_pair>& tensix_cores,
    const size_t tensix_harvesting_mask,
    const tt_xy_pair& dram_grid_size,
    const std::vector<tt_xy_pair>& dram_cores,
    const size_t dram_harvesting_mask,
    const tt_xy_pair& eth_grid_size,
    const std::vector<tt_xy_pair>& eth_cores,
    const tt_xy_pair& arc_grid_size,
    const std::vector<tt_xy_pair>& arc_cores,
    const tt_xy_pair& pcie_grid_size,
    const std::vector<tt_xy_pair>& pcie_cores) {
    switch (arch) {
        case tt::ARCH::GRAYSKULL:
            return std::make_shared<GrayskullCoordinateManager>(
                tensix_grid_size,
                tensix_cores,
                tensix_harvesting_mask,
                dram_grid_size,
                dram_cores,
                dram_harvesting_mask,
                eth_grid_size,
                eth_cores,
                arc_grid_size,
                arc_cores,
                pcie_grid_size,
                pcie_cores);
        case tt::ARCH::WORMHOLE_B0:
            return std::make_shared<WormholeCoordinateManager>(
                tensix_grid_size,
                tensix_cores,
                tensix_harvesting_mask,
                dram_grid_size,
                dram_cores,
                dram_harvesting_mask,
                eth_grid_size,
                eth_cores,
                arc_grid_size,
                arc_cores,
                pcie_grid_size,
                pcie_cores);
        case tt::ARCH::BLACKHOLE:
            return std::make_shared<BlackholeCoordinateManager>(
                tensix_grid_size,
                tensix_cores,
                tensix_harvesting_mask,
                dram_grid_size,
                dram_cores,
                dram_harvesting_mask,
                eth_grid_size,
                eth_cores,
                arc_grid_size,
                arc_cores,
                pcie_grid_size,
                pcie_cores);
        case tt::ARCH::Invalid:
            throw std::runtime_error("Invalid architecture for creating coordinate manager");
    }

    throw std::runtime_error("Invalid architecture for creating coordinate manager");
}
