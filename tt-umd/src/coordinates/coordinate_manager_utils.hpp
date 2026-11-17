// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "tt-umd/coordinates/coordinate_manager.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/common.hpp"

// Helpers the coordinate managers share for walking harvesting masks and building coordinate translations.
namespace tt::umd::coordinate_utils {

// Coordinates paired with the NOC0 location they name.
using CoreTranslations = std::vector<std::pair<CoreCoord, tt_xy_pair>>;

// Every core type a chip can have.
inline constexpr std::array<CoreType, 9> ALL_CORE_TYPES = {
    CoreType::TENSIX,
    CoreType::DRAM,
    CoreType::ETH,
    CoreType::ARC,
    CoreType::PCIE,
    CoreType::ROUTER_ONLY,
    CoreType::SECURITY,
    CoreType::L2CPU,
    CoreType::DISPATCH};

// Coordinate manager for the cores an architecture descriptor lists. Board type and ASIC location are not needed:
// which PCIe core a board uses is carried by the PCIe harvesting mask.
inline std::shared_ptr<CoordinateManager> coordinate_manager_from_arch_descriptor(
    const SocArchDescriptor& arch_descriptor, bool noc_translation_enabled, const HarvestingMasks& harvesting_masks) {
    const std::vector<std::vector<tt_xy_pair>>& dram_cores = arch_descriptor.get_dram_cores();
    return CoordinateManager::create_coordinate_manager(
        arch_descriptor.get_arch(),
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

// Bit index of a harvesting mask names the unit (row, column, bank, channel or core) at that index.
inline bool is_harvested(size_t harvesting_mask, size_t index) {
    return index < sizeof(harvesting_mask) * 8 && ((harvesting_mask >> index) & 1) != 0;
}

inline size_t count_harvested(size_t harvesting_mask, size_t num_units) {
    size_t num_harvested = 0;
    for (size_t index = 0; index < num_units; index++) {
        num_harvested += is_harvested(harvesting_mask, index) ? 1 : 0;
    }
    return num_harvested;
}

// Indices of the harvested (or working) units, in index order.
inline std::vector<size_t> filter_indices_by_mask(size_t num_units, size_t harvesting_mask, bool harvested) {
    std::vector<size_t> indices;
    for (size_t index = 0; index < num_units; index++) {
        if (is_harvested(harvesting_mask, index) == harvested) {
            indices.push_back(index);
        }
    }
    return indices;
}

// Unit indices with the working units first and the harvested ones after them, each in index order.
inline std::vector<size_t> working_then_harvested(size_t num_units, size_t harvesting_mask) {
    std::vector<size_t> order = filter_indices_by_mask(num_units, harvesting_mask, false);
    const std::vector<size_t> harvested = filter_indices_by_mask(num_units, harvesting_mask, true);
    order.insert(order.end(), harvested.begin(), harvested.end());
    return order;
}

inline std::vector<tt_xy_pair> filter_cores_by_mask(
    const std::vector<tt_xy_pair>& cores, size_t harvesting_mask, bool harvested) {
    std::vector<tt_xy_pair> filtered;
    for (size_t index = 0; index < cores.size(); index++) {
        if (is_harvested(harvesting_mask, index) == harvested) {
            filtered.push_back(cores[index]);
        }
    }
    return filtered;
}

// Cores of a grid, in list order, whose harvesting unit (row, column or bank) is harvested or working. unit_of maps
// a core's list index to its unit.
template <typename UnitOf>
std::vector<CoreCoord> filter_grid_cores_by_mask(
    const std::vector<tt_xy_pair>& cores, CoreType core_type, size_t harvesting_mask, bool harvested, UnitOf unit_of) {
    std::vector<CoreCoord> filtered;
    for (size_t index = 0; index < cores.size(); index++) {
        if (is_harvested(harvesting_mask, unit_of(index)) == harvested) {
            filtered.emplace_back(cores[index], core_type, CoordSystem::NOC0);
        }
    }
    return filtered;
}

// Cores sorted by NOC0 position, row by row. Harvesting masks of core types listed in no fixed order (L2CPU) index
// this order, so a mask names the same cores whichever descriptor listed them.
inline std::vector<tt_xy_pair> sort_by_noc0_position(std::vector<tt_xy_pair> cores) {
    std::sort(cores.begin(), cores.end(), [](const tt_xy_pair& a, const tt_xy_pair& b) {
        return std::make_pair(a.y, a.x) < std::make_pair(b.y, b.x);
    });
    return cores;
}

inline CoreTranslations identity_translations(const std::vector<tt_xy_pair>& cores, CoreType core_type) {
    CoreTranslations translations;
    for (const tt_xy_pair& core : cores) {
        translations.emplace_back(CoreCoord(core, core_type, CoordSystem::TRANSLATED), core);
    }
    return translations;
}

// Translated coordinates of a core type whose working cores are packed into one translated row starting at
// (start_x, y) in list order, while harvested cores keep their NOC0 coordinates.
inline CoreTranslations working_row_translations(
    const std::vector<tt_xy_pair>& cores, CoreType core_type, size_t harvesting_mask, size_t start_x, size_t y) {
    CoreTranslations translations;
    const std::vector<tt_xy_pair> working_cores = filter_cores_by_mask(cores, harvesting_mask, false);
    for (size_t index = 0; index < working_cores.size(); index++) {
        translations.emplace_back(
            CoreCoord(start_x + index, y, core_type, CoordSystem::TRANSLATED), working_cores[index]);
    }
    const CoreTranslations harvested =
        identity_translations(filter_cores_by_mask(cores, harvesting_mask, true), core_type);
    translations.insert(translations.end(), harvested.begin(), harvested.end());
    return translations;
}

// Logical coordinates of a core type without a grid: the k-th core is at (k, 0).
inline CoreTranslations row_logical_translations(const std::vector<tt_xy_pair>& cores, CoreType core_type) {
    CoreTranslations translations;
    for (size_t index = 0; index < cores.size(); index++) {
        translations.emplace_back(CoreCoord(index, 0, core_type, CoordSystem::LOGICAL), cores[index]);
    }
    return translations;
}

}  // namespace tt::umd::coordinate_utils
