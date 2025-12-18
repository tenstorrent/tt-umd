// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "umd/device/cluster.hpp"

namespace tt::umd::test::utils {

inline constexpr size_t ONE_KB = 1 << 10;
inline constexpr size_t ONE_MB = 1 << 20;

}  // namespace tt::umd::test::utils
