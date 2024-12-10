/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {
class BlackholeTTDevice : public TTDevice {
public:
    BlackholeTTDevice(std::unique_ptr<PCIDevice> pci_device);
    ~BlackholeTTDevice();

    void configure_iatu_region(size_t region, uint64_t base, uint64_t target, size_t size) override;
};
}  // namespace tt::umd
