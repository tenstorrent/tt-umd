// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TT_KMD_LIB_PCI_IDS_H_
#define TT_KMD_LIB_PCI_IDS_H_

#include <stdint.h>

/**
 * @brief PCI device IDs for Tenstorrent hardware.
 */
static const uint16_t TT_WORMHOLE_PCI_DEVICE_ID = 0x401e;
static const uint16_t TT_BLACKHOLE_PCI_DEVICE_ID = 0xb140;
static const uint16_t TT_GRENDEL_PCI_DEVICE_ID = 0xfeed;

#endif  // TT_KMD_LIB_PCI_IDS_H_
