/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace tt::umd {

/**
 * @brief architecture Enums
 */
enum class architecture {
    jawbridge = 0,
    grayskull = 1,
    wormhole = 2,
    wormhole_b0 = 3,
    blackhole = 4,
    invalid = 0xFF,
};

}  // namespace tt::umd
