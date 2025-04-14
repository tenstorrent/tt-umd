/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

namespace tt::umd {

struct HarvestingMasks {
    size_t tensix_harvesting_mask = 0;
    size_t dram_harvesting_mask = 0;
    size_t eth_harvesting_mask = 0;
    size_t pcie_harvesting_mask = 0;
};

}  // namespace tt::umd
