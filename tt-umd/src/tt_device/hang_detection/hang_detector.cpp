// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/hang_detection/hang_detector.hpp"

#include <cstdint>
#include <optional>

#include "common/unsupported.hpp"

namespace tt::umd {

std::optional<bool> HangDetector::is_bus_hung(uint32_t data_read) { UMD_NOT_IMPLEMENTED(); }

std::optional<bool> HangDetector::is_noc_hung(NocId noc) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
