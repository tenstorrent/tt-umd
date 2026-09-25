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

/**
 * @brief Quasar, whichever chiplet combination the package is built from.
 *
 * Quasar is one architecture that ships as several chiplet combinations, so every one of them
 * presents this id. The kernel driver names the id after the combination it was brought up on
 * (Keraunos), which is a name for a package rather than for an architecture.
 */
static const uint16_t TT_QUASAR_PCI_DEVICE_ID = 0xfeed;

// This header is included from C, where the [[deprecated]] attribute is not available.
#ifdef __cplusplus
[[deprecated("Use TT_QUASAR_PCI_DEVICE_ID instead.")]]
#endif
static const uint16_t TT_GRENDEL_PCI_DEVICE_ID = 0xfeed;

#endif  // TT_KMD_LIB_PCI_IDS_H_
