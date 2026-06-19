// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

/**
 * Filter an ETH status vector to only include non-harvested cores.
 * Uses the SocDescriptor's harvesting information to remove entries
 * for ETH cores that have been harvested.
 */
inline std::vector<std::pair<CoreCoord, bool>> filter_harvested_eth_status(
    const std::vector<std::pair<CoreCoord, bool>>& statuses, const SocDescriptor& soc_desc) {
    auto harvested = soc_desc.get_harvested_cores(CoreType::ETH, CoordSystem::NOC0);

    std::vector<std::pair<CoreCoord, bool>> filtered;
    filtered.reserve(statuses.size());
    std::copy_if(statuses.begin(), statuses.end(), std::back_inserter(filtered), [&](const auto& entry) {
        return std::find(harvested.begin(), harvested.end(), entry.first) == harvested.end();
    });
    return filtered;
}

}  // namespace tt::umd
