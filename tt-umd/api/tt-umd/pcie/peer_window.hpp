// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/noc_id.hpp"

struct tt_device_t;
struct tt_tlb_t;

namespace tt::umd {

class TTDevice;

/**
 * @brief A range of one device's core memory that another device reaches directly over peer-to-peer PCIe.
 *
 * The target device dedicates one of its TLB windows to the range and the initiator device is given a PCIe
 * address for that window, so the initiator's cores read and write the target's memory without the host in
 * between. The window and the mapping belong to this object alone: once constructed it depends on neither
 * TTDevice, leaves their own IO windows untouched, and does not keep either device powered up.
 *
 * Supported only between two different Blackhole devices accessed over PCIe.
 */
class PeerWindow {
public:
    /**
     * @brief Makes [addr, addr + size) of @p core on @p target reachable by @p initiator.
     *
     * @param initiator Device whose traffic reaches the range.
     * @param target Device holding the range; a different device from @p initiator.
     * @param core Core on @p target to reach.
     * @param addr Address within the core the range starts at; must be host-page-aligned.
     * @param size Number of bytes to make reachable; must be host-page-aligned and non-zero, and the range must
     * fit in one TLB window of @p target.
     * @param ordering Ordering of the initiator's transactions through the window.
     * @param noc_id NOC @p target routes the initiator's traffic over.
     * @throws error::RuntimeError if either device is not a Blackhole accessed over PCIe, both are the same
     * device, @p addr or @p size breaks the rules above, or the kernel driver cannot provide the window or map
     * it for @p initiator.
     */
    PeerWindow(
        TTDevice &initiator,
        TTDevice &target,
        CoreCoord core,
        uint64_t addr,
        size_t size,
        IoOrdering ordering = IoOrdering::RELAXED,
        NocId noc_id = NocId::DEFAULT_NOC);

    /**
     * Unmaps the range from the initiator before the target's window is released, so the initiator can never
     * reach memory the window is later aimed at for someone else. The initiator must stop sending traffic to
     * get_address() and let it drain first.
     *
     * If the process dies, or either device is reset while the PeerWindow is alive, the kernel driver releases
     * the window and the mapping in an order this class cannot control, so destroy the PeerWindow before
     * resetting either device.
     */
    ~PeerWindow() noexcept;

    PeerWindow(const PeerWindow &) = delete;
    PeerWindow &operator=(const PeerWindow &) = delete;
    PeerWindow(PeerWindow &&) = delete;
    PeerWindow &operator=(PeerWindow &&) = delete;

    /**
     * @return The address at which the initiator reaches the constructor's addr: the initiator's NOC traffic to
     * its own PCIe core at [get_address(), get_address() + size) lands in [addr, addr + size) of the target core.
     */
    uint64_t get_address() const;

private:
    tt_device_t *initiator_handle_ = nullptr;
    tt_device_t *target_handle_ = nullptr;
    tt_tlb_t *window_ = nullptr;
    uint64_t address_ = 0;
};

}  // namespace tt::umd
