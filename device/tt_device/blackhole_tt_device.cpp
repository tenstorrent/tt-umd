// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/blackhole_tt_device.h"

#include <sys/mman.h>  // for MAP_FAILED

#include "umd/device/blackhole_implementation.h"

namespace tt::umd {

BlackholeTTDevice::BlackholeTTDevice(std::unique_ptr<PCIDevice> pci_device) :
    TTDevice(std::move(pci_device), std::make_unique<blackhole_implementation>()) {}

BlackholeTTDevice::~BlackholeTTDevice() {
    if (pci_device_->bar2_uc != nullptr && pci_device_->bar2_uc != MAP_FAILED) {
        // Disable ATU index 0
        // TODO: Implement disabling for all indexes, once more host channels are enabled.

        // This is not going to happen if the application crashes, so if it's
        // essential for correctness then it needs to move to the driver.
        uint64_t iatu_index = 0;
        uint64_t iatu_base = UNROLL_ATU_OFFSET_BAR + iatu_index * 0x200;
        uint32_t region_ctrl_2 = 0 << 31;  // REGION_EN = 0

        volatile uint32_t *dest =
            reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(pci_device_->bar2_uc) + iatu_base + 0x04);
        const uint32_t *src = &region_ctrl_2;
        *dest = *src;
    }
}
}  // namespace tt::umd
