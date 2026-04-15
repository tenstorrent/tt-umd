/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_device/hang_detection/hang_detector.hpp"

namespace tt::umd {

// Wormhole variant: reads BAR and NOC node ID from the ARC tile.
class WormholeHangDetector : public HangDetector {
public:
    WormholeHangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl);

private:
    uint32_t read_hang_check_reg_via_bar() override;
    uint32_t read_hang_check_reg_via_noc(NocId noc) override;

    static tt_xy_pair get_hang_check_core(NocId noc);
};

}  // namespace tt::umd
