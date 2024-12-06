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
};
}  // namespace tt::umd
