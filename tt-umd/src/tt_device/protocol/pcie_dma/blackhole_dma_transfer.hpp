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
 * D2H is not supported. H2D has the engine signal completion with an MSI write
 * into a host memory flag, so polling it does not cross PCIe.
 */
struct BlackholeDmaTransfer {
    [[noreturn]] void d2h_start(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint64_t dst, uint32_t src, size_t size);
    [[noreturn]] bool d2h_is_complete(const volatile uint8_t* bar2, const DmaBuffer& dma_buffer);

    void h2d_start(volatile uint8_t* bar2, DmaBuffer& dma_buffer, uint32_t dst, uint64_t src, size_t size);
    bool h2d_is_complete(const volatile uint8_t* bar2, const DmaBuffer& dma_buffer);
};

}  // namespace tt::umd
