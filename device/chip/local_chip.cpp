/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.h"

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

LocalChip::LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id) :
    Chip(soc_descriptor), tt_device_(TTDevice::create(pci_device_id)) {}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

bool LocalChip::is_mmio_capable() const { return true; }

}  // namespace tt::umd
