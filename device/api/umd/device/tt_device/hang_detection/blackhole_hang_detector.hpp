/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_device/hang_detection/hang_detector.hpp"

namespace tt::umd {

// Blackhole variant: reads BAR and NOC node ID from the PCIe tile.
class BlackholeHangDetector : public HangDetector {
public:
    BlackholeHangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl, tt_xy_pair pcie_core);

private:
    uint32_t read_hang_check_reg_via_bar() override;
    uint32_t read_hang_check_reg_via_noc(NocId noc) override;
};

}  // namespace tt::umd
