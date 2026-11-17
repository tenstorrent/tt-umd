// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "tt_device/hang_detection/hang_detector_implementation.hpp"

namespace tt::umd {

/**
 * @brief Blackhole hang detector: probes a register in the ARC tile, which answers on both the BAR
 * and the NOC for as long as the chip is alive.
 */
class BlackholeHangDetector : public HangDetectorImplementation {
public:
    BlackholeHangDetector(DeviceProtocol* device_protocol, PcieInterface* pcie_interface);

protected:
    uint32_t read_hang_check_reg_via_bar() override;
    uint32_t read_hang_check_reg_via_noc(NocId noc) override;
};

}  // namespace tt::umd
