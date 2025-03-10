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

    ChipInfo get_chip_info() override;

    uint32_t get_clock() override;

    BoardType get_board_type() override;
};
}  // namespace tt::umd
