// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class DmaInterface {
public:
    virtual ~DmaInterface() = default;
    [[nodiscard]] virtual bool dma_write(
        const void *src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;
    [[nodiscard]] virtual bool dma_read(void *dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;
    [[nodiscard]] virtual bool dma_multicast_write(
        const void *src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) = 0;
    virtual void dma_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;
    virtual void dma_read_zero_copy(
        uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) = 0;
    virtual void dma_multicast_write_zero_copy(
        uint64_t src_iova,
        uint64_t dst_addr,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        NocId noc_id) = 0;
};

}  // namespace tt::umd
