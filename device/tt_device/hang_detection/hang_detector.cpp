/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/hang_detector.hpp"

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"

namespace tt::umd {

HangDetector::HangDetector(
    DeviceProtocol* protocol, architecture_implementation* arch_impl, tt_xy_pair hang_check_core) :
    protocol_(protocol), arch_impl_(arch_impl), hang_check_core_(hang_check_core) {}

std::optional<bool> HangDetector::is_pcie_hung(uint32_t data_read) {
    if (data_read != HANG_READ_VALUE) {
        return false;
    }
    if (!dynamic_cast<PcieInterface*>(protocol_)) {
        return std::nullopt;
    }
    return read_hang_check_reg_via_bar() == HANG_READ_VALUE;
}

std::optional<bool> HangDetector::is_noc_hung(NocId noc) {
    NocIdSwitcher switcher(noc);
    return read_hang_check_reg_via_noc() == HANG_READ_VALUE;
}

}  // namespace tt::umd
