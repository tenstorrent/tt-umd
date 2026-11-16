// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

class PCIDevice;

/** One host-to-device copy. @c src_iova, @c addr and @c size must be multiples of 4. */
struct DmaWrite {
    /** Where to read: SysmemBuffer::get_iova() of a buffer of the same device. */
    uint64_t src_iova = 0;
    /** Endpoint to write, in the coordinates the device's NOC uses (translated ones when translation is on). */
    tt_xy_pair core;
    /** Address at the endpoint to write. */
    uint64_t addr = 0;
    uint32_t size = 0;
    NocId noc = NocId::NOC0;
};

/**
 * What a caller is about to write, so that the queue can ask the driver for address windows that fit it.
 *
 * The driver copies through a fixed set of equally sized windows, each mapping one aligned block of one endpoint's
 * address space, and runs writes together only while they fit the set. Writes that fit it run as one chain per
 * submit(); the rest still land, but in several chains, which is slower.
 */
struct DmaTraffic {
    /** Endpoints the writes of one submit() go to, such as the DRAM banks of a chip. */
    uint32_t endpoints = 8;
    /**
     * Bytes of an endpoint's address space the writes of one submit() fall in, counting whole 2 MiB aligned blocks:
     * 2 MiB when they stay within one such block, 4 MiB when they straddle two.
     */
    uint64_t range = uint64_t{1} << 21;
};

/**
 * Host-to-device DMA writes that the kernel driver queues and runs on the Blackhole DMA engine, submitted in batches
 * and completed asynchronously. Needs a driver with DMA write commands and Linux 6.6 or newer.
 *
 * Batches of a device complete in the order they were submitted. A queue is not thread safe, and a device takes one
 * queue at a time, since the windows the driver uses are per device.
 */
class DmaQueue {
public:
    /** Throws if the driver has no DMA write commands or could not provide any window. */
    explicit DmaQueue(PCIDevice& device, const DmaTraffic& traffic = {});

    /** Waits for the batches still in flight. */
    ~DmaQueue();

    DmaQueue(const DmaQueue&) = delete;
    DmaQueue& operator=(const DmaQueue&) = delete;

    /**
     * Queues the writes as one batch and returns its ticket: 1 for the first batch, and one more for each next one.
     * The writes run in order. Their source memory must stay pinned and unchanged until the batch completes.
     */
    uint64_t submit(const DmaWrite* writes, size_t count);

    uint64_t submit(const std::vector<DmaWrite>& writes) { return submit(writes.data(), writes.size()); }

    /**
     * Returns how many batches have completed, which is the ticket of the last completed one; does not block.
     * Throws if a batch failed, in which case any part of that batch may have been written.
     */
    uint64_t completed();

    /** Blocks until the batch with this ticket has completed. Throws as completed() does. */
    void wait(uint64_t ticket);

    /** Size and number of the windows the driver granted, which is what the batches are shaped to. */
    uint64_t window_size() const;
    uint32_t window_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tt::umd
