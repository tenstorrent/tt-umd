/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief A range of one device's core memory that another device reaches directly over peer-to-peer PCIe.
 *
 * Owns the target device's window and the initiator device's mapping of it. Once created it depends on neither
 * TTDevice nor the interface that created it, leaves their own IO windows untouched and keeps neither device
 * powered up.
 *
 * Destruction unmaps the range from the initiator before the target's window is released, so with a translating
 * IOMMU the initiator can never reach memory the window is later aimed at for someone else; without one the
 * mapping is a plain bus address that unmapping does not revoke. The initiator must stop sending traffic to
 * get_address() and let it drain first. If the process dies, or either device is reset while the window is
 * alive, the kernel driver releases the window and the mapping in an order this class cannot control, so destroy
 * the window before resetting either device.
 */
class PeerWindow {
public:
    virtual ~PeerWindow() = default;

    PeerWindow(const PeerWindow &) = delete;
    PeerWindow &operator=(const PeerWindow &) = delete;
    PeerWindow(PeerWindow &&) = delete;
    PeerWindow &operator=(PeerWindow &&) = delete;

    /**
     * @return The address at which the initiator reaches the start of the range: the initiator's NOC traffic to
     * its own PCIe core at [get_address(), get_address() + size) lands in [addr, addr + size) of the target core.
     */
    virtual uint64_t get_address() const = 0;

protected:
    PeerWindow() = default;
};

/**
 * @brief Peer device access to device memory, such as an RDMA NIC reading from or writing into it directly.
 *
 * Optional: available only on devices whose transport can hand a memory window to a peer.
 */
class PeerDmaInterface {
public:
    virtual ~PeerDmaInterface() = default;

    /**
     * @brief Exports a device memory window as a Linux dma-buf file descriptor a peer device can DMA to and from.
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
     * @brief Makes a device memory range reachable by another Tenstorrent device over peer-to-peer PCIe.
     *
     * @param device_id PCIe device number (/dev/tenstorrent/<device_id>) of the device that issues the traffic:
     * another device of the same architecture, with its own PCIe link.
     * @param core Core to target.
     * @param addr Address within the core the range starts at; must be host-page-aligned.
     * @param size Number of bytes to make reachable; must be host-page-aligned and non-zero, and the range must
     * fit in one window this device can map for a peer (on Blackhole, one 2 MiB-aligned 2 MiB block).
     * @param ordering Ordering of the peer's transactions through the window.
     * @param noc_id NOC to route the peer's traffic over.
     * @return The window; see PeerWindow for its lifetime and teardown guarantees.
     * @throws error::RuntimeError if this device cannot map windows for a peer device, @p device_id names this
     * device or no device of the same architecture, @p addr or @p size breaks the rules above, or the kernel
     * driver cannot provide the window or map it for the peer.
     */
    virtual std::unique_ptr<PeerWindow> export_window_to_device(
        int device_id, tt_xy_pair core, uint64_t addr, size_t size, IoOrdering ordering, NocId noc_id) = 0;
};

}  // namespace tt::umd
