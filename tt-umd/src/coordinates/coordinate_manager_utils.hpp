// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/xy_pair.hpp"

// Helpers the coordinate managers share for walking harvesting masks and building coordinate translations.
namespace tt::umd::coordinate_utils {

// Coordinates paired with the NOC0 location they name.
using CoreTranslations = std::vector<std::pair<CoreCoord, tt_xy_pair>>;

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

inline CoreTranslations identity_translations(const std::vector<tt_xy_pair>& cores, CoreType core_type) {
    CoreTranslations translations;
    for (const tt_xy_pair& core : cores) {
        translations.emplace_back(CoreCoord(core, core_type, CoordSystem::TRANSLATED), core);
    }
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
