/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <set>

#include "umd/device/blackhole_arc_telemetry_reader.h"
#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {
class BlackholeTTDevice : public TTDevice {
public:
    BlackholeTTDevice(std::unique_ptr<PCIDevice> pci_device);
    ~BlackholeTTDevice();

    void configure_iatu_region(size_t region, uint64_t base, uint64_t target, size_t size) override;

    ChipInfo get_chip_info() override;

    void wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms = 1000) override;

    uint32_t get_clock() override;

    uint32_t get_max_clock_freq() override;

    uint32_t get_min_clock_freq() override;

    BoardType get_board_type() override;

private:
    static constexpr uint64_t ATU_OFFSET_IN_BH_BAR2 = 0x1200;
    std::set<size_t> iatu_regions_;
};
}  // namespace tt::umd
