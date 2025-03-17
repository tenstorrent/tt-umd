/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/tt_device/tt_device.h"
#include <mutex>

namespace tt::umd {
class WormholeTTDevice : public TTDevice {
    std::mutex dma_mutex_;
public:
    WormholeTTDevice(std::unique_ptr<PCIDevice> pci_device);

    void wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms = 1000) override;

    ChipInfo get_chip_info() override;

    uint32_t get_clock() override;

    BoardType get_board_type() override;

    void dma_d2h(void *dst, uint32_t src, size_t size) override;
    void dma_h2d(uint32_t dst, const void *src, size_t size) override;
};
}  // namespace tt::umd
