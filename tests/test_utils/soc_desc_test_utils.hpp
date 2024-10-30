/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

namespace test_utils {

static std::size_t get_num_harvested(std::size_t harvesting_mask) {
    // Counts the number of 1 bits in harvesting mask, effectively representing
    // the number of harvested rows (Wormhole) or columns (Blackhole).
    return __builtin_popcount(harvesting_mask);
}

}
