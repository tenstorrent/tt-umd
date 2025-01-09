/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

namespace tt::umd {

struct SoftwareHarvesting {
    uint32_t tensix_harvesting_mask;
    uint32_t eth_harvesting_mask;
    uint32_t dram_harvesting_mask;
};

}  // namespace tt::umd
