// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/warm_reset.hpp"

namespace tt::umd {

// NOLINTNEXTLINE(performance-unnecessary-value-param).
bool WarmReset::warm_reset(std::vector<int>, bool, bool, std::chrono::milliseconds) { return true; }

}  // namespace tt::umd
