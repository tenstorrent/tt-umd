// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/hang_detection/blackhole_hang_detector.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

BlackholeHangDetector::BlackholeHangDetector(DeviceProtocol* device_protocol, PcieInterface* pcie_interface) :
    HangDetectorImplementation(device_protocol, pcie_interface) {}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_bar() {
    UMD_THROW(error::RuntimeError, "BlackholeHangDetector::read_hang_check_reg_via_bar is not implemented yet");
}

uint32_t BlackholeHangDetector::read_hang_check_reg_via_noc(NocId noc) {
    UMD_THROW(error::RuntimeError, "BlackholeHangDetector::read_hang_check_reg_via_noc is not implemented yet");
}

}  // namespace tt::umd
