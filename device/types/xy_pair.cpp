// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/types/xy_pair.hpp"

#include <string>

namespace tt {

std::string xy_pair::str() const { return std::to_string(x) + "-" + std::to_string(y); }

std::string cxy_pair::str() const { return std::to_string(chip) + ":" + std::to_string(x) + "-" + std::to_string(y); }

}  // namespace tt
