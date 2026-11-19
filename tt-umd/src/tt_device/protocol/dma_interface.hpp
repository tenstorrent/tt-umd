/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/types/dma.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief DMA transfer operations between host and device memory.
 *
 * Two transfer modes:
 * - Bounce buffer: copies through an internal pinned staging buffer. Returns false if DMA is
 *   unavailable, letting the caller fall back.
 * - Zero-copy: operates directly on caller-managed pinned memory identified by IOVA, bypassing
 *   the staging buffer. Also available asynchronously, as a start/check pair per direction.
 */
class DmaInterface {
public:
    virtual ~DmaInterface() = default;

    /**
     * @brief Device-to-Host DMA via internal staging buffer.
     * @param dst Pointer to the destination host buffer.
     * @param src_addr Source address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinates.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    [[nodiscard]] virtual bool dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Host-to-Device DMA via internal staging buffer.
     * @param src Pointer to the source host memory.
     * @param dst_addr Destination address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Target core coordinates.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    [[nodiscard]] virtual bool dma_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Multicast Host-to-Device DMA via internal staging buffer.
     * @param src Pointer to the source host memory.
     * @param dst_addr Destination address on the target cores.
     * @param size Number of bytes to transfer.
     * @param core_start Top-left core of the multicast rectangle.
     * @param core_end Bottom-right core of the multicast rectangle.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    [[nodiscard]] virtual bool dma_multicast_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) = 0;

    /**
     * @brief Zero-copy Device-to-Host DMA into caller-managed pinned memory.
     * @param dst_iova IOVA of the destination pinned host memory buffer.
     * @param src_addr Source address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinates.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    virtual bool dma_read_zero_copy(
        uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Zero-copy Host-to-Device DMA from caller-managed pinned memory.
     * @param src_iova IOVA of the source pinned host memory buffer.
     * @param dst_addr Destination address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Target core coordinates.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    virtual bool dma_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Zero-copy multicast Host-to-Device DMA from caller-managed pinned memory.
     * @param src_iova IOVA of the source pinned host memory buffer.
     * @param dst_addr Destination address on the target cores.
     * @param size Number of bytes to transfer.
     * @param core_start Top-left core of the multicast rectangle.
     * @param core_end Bottom-right core of the multicast rectangle.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    virtual bool dma_multicast_write_zero_copy(
        uint64_t src_iova,
        uint64_t dst_addr,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        NocId noc_id) = 0;

    /**
     * @brief Starts a zero-copy H2D DMA transfer.
     *
     * The device runs one transfer at a time, and a transfer can move at most dma_zero_copy_max_size()
     * bytes. The thread that started it owns it: only that thread is told it finished. Host memory must
     * stay pinned and unchanged until then.
     *
     * @return true if the transfer was started; false if the device is busy, so nothing was started
     *         and the caller can retry.
     * @throws if DMA is unavailable on this device, or the transfer is invalid.
     */
    [[nodiscard]] virtual bool dma_write_zero_copy_start(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Zero-copy D2H counterpart of dma_write_zero_copy_start(). Host memory must not be read
     * until the transfer finishes.
     */
    [[nodiscard]] virtual bool dma_read_zero_copy_start(
        uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Reports on the H2D transfer in flight, without waiting. The owning thread is told
     * FINISHED or ERROR exactly once; other threads only observe.
     */
    [[nodiscard]] virtual DmaTransferState dma_write_zero_copy_check() = 0;

    /**
     * @brief D2H counterpart of dma_write_zero_copy_check().
     */
    [[nodiscard]] virtual DmaTransferState dma_read_zero_copy_check() = 0;

    /**
     * @brief Most bytes a single asynchronous zero-copy transfer starting at device_addr can move.
     */
    [[nodiscard]] virtual size_t dma_zero_copy_max_size(uint64_t device_addr) const = 0;
};

}  // namespace tt::umd
