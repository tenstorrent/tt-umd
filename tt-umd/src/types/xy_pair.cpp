// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/types/xy_pair.hpp"

#include <fmt/format.h>

#include <string>

namespace tt {

std::string xy_pair::str() const { return fmt::format("({}, {})", x, y); }

std::string cxy_pair::str() const { return fmt::format("(chip {}, {}, {})", chip, x, y); }

}  // namespace tt
