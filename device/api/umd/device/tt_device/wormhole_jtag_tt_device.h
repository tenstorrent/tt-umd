/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "umd/device/tt_device/wormhole_tt_device.h"

namespace tt::umd {
class WormholeJtagTTDevice : public WormholeTTDevice {
public:
    WormholeJtagTTDevice(std::shared_ptr<PCIDevice> pci_device);
    WormholeJtagTTDevice();

    void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void bar_write32(uint32_t addr, uint32_t data) override;

    uint32_t bar_read32(uint32_t addr) override;
};

}  // namespace tt::umd
