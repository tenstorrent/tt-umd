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
 * Blackhole DMA transfer strategy.
 *
 * Programs the Blackhole-specific DMA controller via BAR2 (uncached).
 * D2H is not supported. H2D uses MSI-based interrupt setup and polls
 * the XFERSIZE register for completion.
 */
struct BlackholeDmaTransfer {
    [[noreturn]] void d2h_start(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint64_t dst, uint32_t src, size_t size);
    [[noreturn]] bool d2h_is_complete(const volatile uint8_t* bar2, const DmaBuffer& dma_buffer);

    void h2d_start(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint32_t dst, uint64_t src, size_t size);
    // Reads XFERSIZE back over PCIe, so each call costs a device round trip.
    bool h2d_is_complete(const volatile uint8_t* bar2, const DmaBuffer& dma_buffer);
};

}  // namespace tt::umd
