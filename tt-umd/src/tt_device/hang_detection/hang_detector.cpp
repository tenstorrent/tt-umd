// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/hang_detection/hang_detector.hpp"

namespace tt::umd {

std::optional<bool> HangDetector::is_bus_hung(uint32_t data_read) {
    if (!is_bus_available()) {
        return std::nullopt;
    }
    // Only an all-ones read can come from a hung bus, so any other value clears it without a device read.
    if (data_read != HANG_READ_VALUE) {
        return false;
    }
    return read_hang_check_reg_via_bar() == HANG_READ_VALUE;
}

std::optional<bool> HangDetector::is_noc_hung(NocId noc) {
    if (!is_noc_available()) {
        return std::nullopt;
    }
    return read_hang_check_reg_via_noc(noc) == HANG_READ_VALUE;
}

}  // namespace tt::umd
