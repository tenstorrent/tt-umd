// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt_enums_structs_constants_doxy.hpp"

namespace tt::umd {

/**
 * @defgroup tt_dma_interface DmaInterface
 * @{
 *
 * @brief DMA transfer operations between host and device memory.
 *
 * Two transfer modes:
 * - **Bounce buffer** — copies through an internal pinned staging buffer.
 *   Returns false if DMA is unavailable, letting the caller fall back.
 * - **Zero-copy** — operates directly on caller-managed pinned memory
 *   identified by IOVA, bypassing the staging buffer.
 *
 * All core coordinates are tt_xy_pair — raw (x, y) with no translation.
 *
 * @optional
 *
 */

/**
 * @brief DMA transfer interface.
 */
class DmaInterface {
public:
    virtual ~DmaInterface() = default;

    /** @name Bounce Buffer Transfers */
    /** @{ */

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
        const void *src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

    /**
     * @brief Device-to-Host DMA via internal staging buffer.
     * @param dst Pointer to the destination host buffer.
     * @param src_addr Source address on the target core.
     * @param size Number of bytes to transfer.
     * @param core Source core coordinates.
     * @param noc_id NOC to route through.
     * @return true on success; false if DMA is unavailable.
     */
    [[nodiscard]] virtual bool dma_read(void *dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;

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
        const void *src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) = 0;

    /** @} */

    /** @name Zero-Copy Transfers */
    /** @{ */

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

    /** @} */
};

/** @} */  // end of tt_dma_interface group

}  // namespace tt::umd
