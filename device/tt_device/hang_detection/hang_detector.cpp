/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/hang_detector.hpp"

#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

std::optional<bool> HangDetector::is_bus_hung(uint32_t data_read) {
    if (data_read != HANG_READ_VALUE) {
        return false;
    }
    if (!is_bus_available()) {
        return std::nullopt;
    }
    return read_hang_check_reg_via_bar() == HANG_READ_VALUE;
}

std::optional<bool> HangDetector::is_noc_hung(NocId noc) {
    if (!is_noc_available()) {
        return std::nullopt;
    }
    NocIdSwitcher switcher(noc);
    auto result = read_hang_check_reg_via_noc(noc);
    return result == HANG_READ_VALUE;
}

}  // namespace tt::umd
