// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef UMD_DEVICE_PCIE_PCI_IDS_H_
#define UMD_DEVICE_PCIE_PCI_IDS_H_

#include <stdint.h>

/**
 * @brief PCI device IDs for Tenstorrent hardware.
 */
static const uint16_t TT_WORMHOLE_PCI_DEVICE_ID = 0x401e;
static const uint16_t TT_BLACKHOLE_PCI_DEVICE_ID = 0xb140;
static const uint16_t TT_GRENDEL_PCI_DEVICE_ID = 0xfeed;

#ifdef __cplusplus

#include "umd/device/types/arch.hpp"

namespace tt::umd {

/**
 * Map a Tenstorrent PCI device ID to its tt::ARCH.
 * Returns tt::ARCH::Invalid for unknown device IDs.
 */
inline tt::ARCH arch_from_pci_device_id(uint16_t device_id) {
    if (device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::WORMHOLE_B0;
    }
    if (device_id == TT_BLACKHOLE_PCI_DEVICE_ID) {
        return tt::ARCH::BLACKHOLE;
    }
    if (device_id == TT_GRENDEL_PCI_DEVICE_ID) {
        return tt::ARCH::QUASAR;
    }
    return tt::ARCH::Invalid;
}

}  // namespace tt::umd

#endif  // __cplusplus

#endif  // UMD_DEVICE_PCIE_PCI_IDS_H_
