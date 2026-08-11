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

#endif  // UMD_DEVICE_PCIE_PCI_IDS_H_
