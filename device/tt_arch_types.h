/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string>

namespace tt {
/**
 * @brief ARCH Enums
 */
enum class ARCH {
    JAWBRIDGE = 0,
    GRAYSKULL = 1,
    WORMHOLE = 2,
    WORMHOLE_B0 = 3,
    BLACKHOLE = 4,
    Invalid = 0xFF,
};
}