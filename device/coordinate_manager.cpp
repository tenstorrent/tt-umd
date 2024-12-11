/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/coordinate_manager.h"

#include "logger.hpp"
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

void CoordinateManager::initialize() {
    this->identity_map_physical_cores();
    this->translate_tensix_coords();
    this->translate_dram_coords();
    this->translate_eth_coords();
    this->translate_arc_coords();
    this->translate_pcie_coords();
}

void CoordinateManager::add_core_translation(const CoreCoord& core_coord, const tt_xy_pair& physical_pair) {
    to_physical_map.insert({core_coord, physical_pair});
    from_physical_map.insert({{{physical_pair.x, physical_pair.y}, core_coord.coord_system}, core_coord});
}

void CoordinateManager::identity_map_physical_cores() {
    for (auto& core : tensix_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::TENSIX, CoordSystem::PHYSICAL);
        add_core_translation(core_coord, core);
    }

    for (auto& core : dram_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::DRAM, CoordSystem::PHYSICAL);
        add_core_translation(core_coord, core);
    }

    for (auto& core : eth_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::ETH, CoordSystem::PHYSICAL);
        add_core_translation(core_coord, core);
    }

    for (auto& core : arc_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::ARC, CoordSystem::PHYSICAL);
        add_core_translation(core_coord, core);
    }

    for (auto& core : pcie_cores) {
        const CoreCoord core_coord = CoreCoord(core.x, core.y, CoreType::PCIE, CoordSystem::PHYSICAL);
        add_core_translation(core_coord, core);
    }
}

CoreCoord CoordinateManager::to(const CoreCoord core_coord, const CoordSystem coord_system) {
    return from_physical_map.at({to_physical_map.at(core_coord), coord_system});
}

void CoordinateManager::translate_tensix_coords() {
    size_t num_harvested_y = CoordinateManager::get_num_harvested(tensix_harvesting_mask);
    size_t grid_size_x = tensix_grid_size.x;
    size_t grid_size_y = tensix_grid_size.y;

    size_t logical_y = 0;
    size_t harvested_index = (grid_size_y - num_harvested_y) * grid_size_x;
    for (size_t y = 0; y < grid_size_y; y++) {
        if (tensix_harvesting_mask & (1 << y)) {
            for (size_t x = 0; x < grid_size_x; x++) {
                const tt_xy_pair& physical_core = tensix_cores[y * grid_size_x + x];
                const tt_xy_pair& virtual_core = tensix_cores[harvested_index++];

                CoreCoord virtual_coord =
                    CoreCoord(virtual_core.x, virtual_core.y, CoreType::TENSIX, CoordSystem::VIRTUAL);

                add_core_translation(virtual_coord, physical_core);
            }
        } else {
            for (size_t x = 0; x < grid_size_x; x++) {
                const tt_xy_pair& tensix_core = tensix_cores[y * grid_size_x + x];
                const tt_xy_pair& virtual_core = tensix_cores[logical_y * grid_size_x + x];

                CoreCoord logical_coord = CoreCoord(x, logical_y, CoreType::TENSIX, CoordSystem::LOGICAL);
                add_core_translation(logical_coord, tensix_core);

                CoreCoord virtual_coord =
                    CoreCoord(virtual_core.x, virtual_core.y, CoreType::TENSIX, CoordSystem::VIRTUAL);
                add_core_translation(virtual_coord, tensix_core);
            }
            logical_y++;
        }
    }

    this->fill_tensix_physical_translated_mapping();
}

void CoordinateManager::fill_tensix_physical_translated_mapping() {
    size_t num_harvested_y = CoordinateManager::get_num_harvested(tensix_harvesting_mask);

    for (size_t x = 0; x < tensix_grid_size.x; x++) {
        for (size_t y = 0; y < tensix_grid_size.y - num_harvested_y; y++) {
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL);
            const tt_xy_pair physical_pair = to_physical_map[logical_coord];
            const size_t translated_x = physical_pair.x;
            const size_t translated_y = physical_pair.y;

            CoreCoord translated_coord =
                CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, physical_pair);
        }
    }

    size_t harvested_index = (tensix_grid_size.y - num_harvested_y) * tensix_grid_size.x;
    for (size_t y = 0; y < tensix_grid_size.y; y++) {
        if (tensix_harvesting_mask & (1 << y)) {
            for (size_t x = 0; x < tensix_grid_size.x; x++) {
                const tt_xy_pair& physical_core = tensix_cores[y * tensix_grid_size.x + x];
                const size_t translated_x = physical_core.x;
                const size_t translated_y = physical_core.y;

                CoreCoord translated_coord =
                    CoreCoord(translated_x, translated_y, CoreType::TENSIX, CoordSystem::TRANSLATED);

                add_core_translation(translated_coord, physical_core);
            }
        }
    }
}

void CoordinateManager::translate_dram_coords() {
    for (size_t x = 0; x < dram_grid_size.x; x++) {
        for (size_t y = 0; y < dram_grid_size.y; y++) {
            const tt_xy_pair dram_core = dram_cores[x * dram_grid_size.y + y];

            CoreCoord logical_coord = CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);
            CoreCoord virtual_coord = CoreCoord(dram_core.x, dram_core.y, CoreType::DRAM, CoordSystem::VIRTUAL);

            add_core_translation(logical_coord, dram_core);
            add_core_translation(virtual_coord, dram_core);
        }
    }

    fill_dram_physical_translated_mapping();
}

void CoordinateManager::translate_eth_coords() {
    for (size_t x = 0; x < eth_grid_size.x; x++) {
        for (size_t y = 0; y < eth_grid_size.y; y++) {
            const tt_xy_pair eth_core = eth_cores[x * eth_grid_size.y + y];

            CoreCoord logical_coord = CoreCoord(x, y, CoreType::ETH, CoordSystem::LOGICAL);
            CoreCoord virtual_coord = CoreCoord(eth_core.x, eth_core.y, CoreType::ETH, CoordSystem::VIRTUAL);

            add_core_translation(logical_coord, eth_core);
            add_core_translation(virtual_coord, eth_core);
        }
    }

    fill_eth_physical_translated_mapping();
}

void CoordinateManager::translate_arc_coords() {
    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            const tt_xy_pair arc_core = arc_cores[x * arc_grid_size.y + y];

            CoreCoord logical_coord = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);
            CoreCoord virtual_coord = CoreCoord(arc_core.x, arc_core.y, CoreType::ARC, CoordSystem::VIRTUAL);

            add_core_translation(logical_coord, arc_core);
            add_core_translation(virtual_coord, arc_core);
        }
    }

    fill_arc_physical_translated_mapping();
}

void CoordinateManager::translate_pcie_coords() {
    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            const tt_xy_pair pcie_core = pcie_cores[x * pcie_grid_size.y + y];
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);
            CoreCoord virtual_coord = CoreCoord(pcie_core.x, pcie_core.y, CoreType::PCIE, CoordSystem::VIRTUAL);

            add_core_translation(logical_coord, pcie_core);
            add_core_translation(virtual_coord, pcie_core);
        }
    }

    fill_pcie_physical_translated_mapping();
}

void CoordinateManager::fill_eth_physical_translated_mapping() {
    for (size_t x = 0; x < eth_grid_size.x; x++) {
        for (size_t y = 0; y < eth_grid_size.y; y++) {
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::ETH, CoordSystem::LOGICAL);
            const tt_xy_pair physical_pair = to_physical_map[logical_coord];
            const size_t translated_x = physical_pair.x;
            const size_t translated_y = physical_pair.y;

            CoreCoord translated_coord = CoreCoord(translated_x, translated_y, CoreType::ETH, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, physical_pair);
        }
    }
}

void CoordinateManager::fill_dram_physical_translated_mapping() {
    for (size_t x = 0; x < dram_grid_size.x; x++) {
        for (size_t y = 0; y < dram_grid_size.y; y++) {
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::DRAM, CoordSystem::LOGICAL);
            const tt_xy_pair physical_pair = to_physical_map[logical_coord];
            const size_t translated_x = physical_pair.x;
            const size_t translated_y = physical_pair.y;

            CoreCoord translated_coord = CoreCoord(translated_x, translated_y, CoreType::DRAM, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, physical_pair);
        }
    }
}

void CoordinateManager::fill_pcie_physical_translated_mapping() {
    for (size_t x = 0; x < pcie_grid_size.x; x++) {
        for (size_t y = 0; y < pcie_grid_size.y; y++) {
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::PCIE, CoordSystem::LOGICAL);
            const tt_xy_pair physical_pair = to_physical_map[logical_coord];
            const size_t translated_x = physical_pair.x;
            const size_t translated_y = physical_pair.y;

            CoreCoord translated_coord = CoreCoord(translated_x, translated_y, CoreType::PCIE, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, physical_pair);
        }
    }
}

void CoordinateManager::fill_arc_physical_translated_mapping() {
    for (size_t x = 0; x < arc_grid_size.x; x++) {
        for (size_t y = 0; y < arc_grid_size.y; y++) {
            CoreCoord logical_coord = CoreCoord(x, y, CoreType::ARC, CoordSystem::LOGICAL);
            const tt_xy_pair physical_pair = to_physical_map[logical_coord];
            const size_t translated_x = physical_pair.x;
            const size_t translated_y = physical_pair.y;

            CoreCoord translated_coord = CoreCoord(translated_x, translated_y, CoreType::ARC, CoordSystem::TRANSLATED);

            add_core_translation(translated_coord, physical_pair);
        }
    }
}

void CoordinateManager::assert_create_coordinate_manager(
    const tt::ARCH arch, const size_t tensix_harvesting_mask, const size_t dram_harvesting_mask) {
    log_assert(
        !(arch != tt::ARCH::BLACKHOLE && dram_harvesting_mask != 0), "DRAM harvesting is supported only for Blackhole");

    if (arch == tt::ARCH::BLACKHOLE) {
        log_assert(get_num_harvested(dram_harvesting_mask) <= 1, "Only one DRAM bank can be harvested on Blackhole");
    }
}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch, const size_t tensix_harvesting_mask, const size_t dram_harvesting_mask) {
    assert_create_coordinate_manager(arch, tensix_harvesting_mask, dram_harvesting_mask);

    switch (arch) {
        case tt::ARCH::GRAYSKULL:
            return create_coordinate_manager(
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
            return create_coordinate_manager(
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
            return create_coordinate_manager(
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
        case tt::ARCH::Invalid:
            throw std::runtime_error("Invalid architecture for creating coordinate manager");
        default:
            throw std::runtime_error("Unexpected ARCH value " + std::to_string((int)arch));
    }
}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
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
    assert_create_coordinate_manager(arch, tensix_harvesting_mask, dram_harvesting_mask);

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
