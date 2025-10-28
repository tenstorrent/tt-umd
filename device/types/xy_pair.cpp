/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/types/xy_pair.hpp"

#include <fmt/core.h>

namespace tt {

std::string xy_pair::str() const { return fmt::format("(x={},y={})", static_cast<int>(x), static_cast<int>(y)); }

std::string cxy_pair::str() const {
    return fmt::format("(chip={},x={},y={})", chip, static_cast<int>(x), static_cast<int>(y));
}

}  // namespace tt
