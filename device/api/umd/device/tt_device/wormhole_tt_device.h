/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <mutex>

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {
class WormholeTTDevice : public TTDevice {
public:
    WormholeTTDevice(std::unique_ptr<PCIDevice> pci_device);

    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    void wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms = 1000) override;

    ChipInfo get_chip_info() override;

    uint32_t get_clock() override;

    uint32_t get_max_clock_freq() override;

    uint32_t get_min_clock_freq() override;

    BoardType get_board_type() override;

    std::vector<DramTrainingStatus> get_dram_training_status() override;

    void dma_d2h(void *dst, uint32_t src, size_t size) override;
    void dma_h2d(uint32_t dst, const void *src, size_t size) override;

private:
    // Enforce single-threaded access, even though there are more serious issues
    // surrounding resource management as it relates to DMA.
    std::mutex dma_mutex_;
};
}  // namespace tt::umd
