/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {
class WormholeTTDevice : public TTDevice {
public:
    WormholeTTDevice(std::unique_ptr<PCIDevice> pci_device);

    void wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms = 1000) override;

    ChipInfo get_chip_info() override;

    uint32_t get_clock() override;

    uint32_t get_max_clock_freq() override;

    uint32_t get_min_clock_freq() override;

    BoardType get_board_type() override;

    std::vector<DramTrainingStatus> get_dram_training_status() override;
};
}  // namespace tt::umd
