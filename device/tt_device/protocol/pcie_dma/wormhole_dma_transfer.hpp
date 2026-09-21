/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::umd {

struct DmaBuffer;

/**
 * Wormhole DMA transfer strategy.
 *
 * Programs the DesignWare DMA controller via BAR2 (uncached) using IMWR-based
 * completion signaling. Supports both D2H (write engine) and H2D (read engine).
 */
struct WormholeDmaTransfer {
    void d2h_transfer(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint64_t dst, uint32_t src, size_t size);
    void h2d_transfer(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint32_t dst, uint64_t src, size_t size);
};

}  // namespace tt::umd
