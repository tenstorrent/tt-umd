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
 *
 * Programming and waiting are separate so that a caller can do something else while the transfer
 * runs. Neither half serializes anything: only one transfer may be in flight on the channel, and it
 * is the caller that has to guarantee it.
 */
struct WormholeDmaTransfer {
    // Programs the descriptor and rings the doorbell. Returns as soon as the transfer is under way.
    void d2h_start(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint64_t dst, uint32_t src, size_t size);

    // Whether the transfer started by d2h_start() has finished. Free of side effects, so it can be
    // called as often as the caller likes.
    bool d2h_is_complete(const volatile uint8_t* bar2, const DmaBuffer& dma_buffer);

    void h2d_start(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint32_t dst, uint64_t src, size_t size);
    bool h2d_is_complete(const volatile uint8_t* bar2, const DmaBuffer& dma_buffer);
};

}  // namespace tt::umd
