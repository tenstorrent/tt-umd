/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

#include "tt-umd/pcie/pci_device.hpp"

namespace tt::umd {

// Marker the DMA engine writes into the completion flag when a transfer finishes. Any value works as long as it is
// not 0, which is what the flag is cleared to before each transfer.
inline constexpr uint32_t DMA_COMPLETION_VALUE = 0xfaca;

inline bool is_dma_completion_flag_set(const DmaBuffer& dma_buffer) {
    return *reinterpret_cast<volatile uint32_t*>(dma_buffer.completion) == DMA_COMPLETION_VALUE;
}

}  // namespace tt::umd
