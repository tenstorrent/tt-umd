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

    void bar_write32(uint32_t addr, uint32_t data);
    uint32_t bar_read32(uint32_t addr);

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);

protected:
    ChipInfo get_chip_info() override;
};
}  // namespace tt::umd
