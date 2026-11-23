/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Peer device access to device memory, such as an RDMA NIC writing into it directly.
 *
 * Optional: available only on devices whose transport can hand a memory window to a peer.
 */
class PeerDmaInterface {
public:
    virtual ~PeerDmaInterface() = default;

    /**
     * @brief Exports a device memory window as a Linux dma-buf file descriptor a peer device can DMA into.
     *
     * @param core Core to target.
     * @param addr Address within the core the window starts at; must be host-page-aligned.
     * @param size Number of bytes to export; must be host-page-aligned and non-zero. The dma-buf is
     * exactly this long.
     * @param ordering Ordering of the peer's transactions through the window.
     * @param noc_id NOC to route the peer's traffic over.
     * @return int The dma-buf fd; the caller owns it and must close() it.
     */
    virtual int export_window(tt_xy_pair core, uint64_t addr, size_t size, IoOrdering ordering, NocId noc_id) = 0;

    /**
     * @brief Maps a peer's exported window, a dma-buf from its export_window(), for this device to DMA into.
     *
     * The mapping holds its own reference on the buffer, so the caller keeps ownership of @p dmabuf_fd and
     * may close it right away. The mapping, and the peer's window behind it, last until this device is closed.
     *
     * @param dmabuf_fd The peer's dma-buf; must not be one this device exported.
     * @return uint64_t The address at which this device reaches the start of the buffer.
     */
    virtual uint64_t import_window(int dmabuf_fd) = 0;
};

}  // namespace tt::umd
