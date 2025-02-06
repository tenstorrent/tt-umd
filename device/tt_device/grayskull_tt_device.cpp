// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/grayskull_tt_device.h"

#include "umd/device/grayskull_implementation.h"

namespace tt::umd {

GrayskullTTDevice::GrayskullTTDevice(std::unique_ptr<PCIDevice> pci_device) :
    TTDevice(std::move(pci_device), std::make_unique<grayskull_implementation>()) {}

ChipInfo GrayskullTTDevice::get_chip_info() {
    throw std::runtime_error("Reading ChipInfo is not supported for Grayskull.");
}

}  // namespace tt::umd
