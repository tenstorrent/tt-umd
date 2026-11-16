// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/dma_queue.hpp"

#include <fmt/format.h>
#include <liburing.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <tt-logger/tt-logger.hpp>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr uint64_t WINDOW_2M = TT_TLB_SIZE_2M;
constexpr uint64_t WINDOW_4G = TT_TLB_SIZE_4G;
// More 2 MiB windows than this are not worth holding: a 4 GiB window per endpoint covers the same traffic.
constexpr uint64_t MAX_2M_WINDOWS = 16;
constexpr unsigned RING_ENTRIES = 64;

uint64_t blocks(uint64_t range, uint64_t block_size) {
    return std::max<uint64_t>(1, (range + block_size - 1) / block_size);
}

// An endpoint and one window-aligned block of its address space: what one window maps.
struct Region {
    uint16_t x;
    uint16_t y;
    uint8_t noc;
    uint64_t base;

    bool operator==(const Region& other) const {
        return x == other.x && y == other.y && noc == other.noc && base == other.base;
    }
};

}  // namespace

struct DmaQueue::Impl {
    struct Batch {
        // The driver reads these when it issues a command, which can be after submit() returns.
        std::vector<tt_dma_write_desc_t> descs;
        unsigned commands_in_flight = 0;
        int error = 0;
    };

    tt_device_t* device = nullptr;
    io_uring ring{};
    uint64_t window_size = 0;
    uint32_t window_count = 0;
    std::deque<Batch> in_flight;
    uint64_t completed = 0;

    // The driver rejects a write that crosses a window boundary, so each write becomes one descriptor per window.
    void cut(const DmaWrite& write, std::vector<tt_dma_write_desc_t>& descs) const {
        for (uint64_t offset = 0; offset < write.size;) {
            const uint64_t addr = write.addr + offset;
            const uint64_t to_boundary = window_size - (addr & (window_size - 1));

            tt_dma_write_desc_t desc{};
            desc.src = write.src_iova + offset;
            desc.addr = addr;
            desc.size = static_cast<uint32_t>(std::min<uint64_t>(write.size - offset, to_boundary));
            desc.x = static_cast<uint16_t>(write.core.x);
            desc.y = static_cast<uint16_t>(write.core.y);
            desc.noc = static_cast<uint8_t>(write.noc);
            descs.push_back(desc);

            offset += desc.size;
        }
    }

    void queue_command(const tt_dma_write_desc_t* descs, size_t count, uint64_t ticket) {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (sqe == nullptr) {
            flush();
            sqe = io_uring_get_sqe(&ring);
        }
        tt_dma_write_prep(device, sqe, descs, static_cast<uint32_t>(count));
        sqe->user_data = ticket;
    }

    void flush() {
        const int ret = io_uring_submit(&ring);
        if (ret < 0) {
            UMD_THROW(error::RuntimeError, fmt::format("Submitting DMA writes failed: {}", strerror(-ret)));
        }
    }

    void complete(const io_uring_cqe* cqe) {
        Batch& batch = in_flight[cqe->user_data - completed - 1];
        batch.commands_in_flight--;
        if (cqe->res < 0 && batch.error == 0) {
            batch.error = cqe->res;
        }
    }

    // Takes the completions that have arrived, or with block set waits for the next one first.
    void reap(bool block) {
        io_uring_cqe* cqe = nullptr;
        if (block) {
            const int ret = io_uring_wait_cqe(&ring, &cqe);
            if (ret < 0) {
                UMD_THROW(error::RuntimeError, fmt::format("Waiting for DMA writes failed: {}", strerror(-ret)));
            }
        }
        while (io_uring_peek_cqe(&ring, &cqe) == 0) {
            complete(cqe);
            io_uring_cqe_seen(&ring, cqe);
        }

        int error = 0;
        while (!in_flight.empty() && in_flight.front().commands_in_flight == 0) {
            if (error == 0) {
                error = in_flight.front().error;
            }
            in_flight.pop_front();
            completed++;
        }
        if (error != 0) {
            UMD_THROW(error::RuntimeError, fmt::format("DMA write failed: {}", strerror(-error)));
        }
    }
};

DmaQueue::DmaQueue(PCIDevice& device, const DmaTraffic& traffic) : impl_(std::make_unique<Impl>()) {
    impl_->device = device.get_tt_device_handle();

    // 2 MiB windows are plentiful, while a device has at most eight 4 GiB ones, shared with every other user.
    uint64_t size = WINDOW_2M;
    uint64_t count = traffic.endpoints * blocks(traffic.range, WINDOW_2M);
    if (count > MAX_2M_WINDOWS) {
        size = WINDOW_4G;
        count = traffic.endpoints * blocks(traffic.range, WINDOW_4G);
    }

    int ret = tt_dma_set_windows(
        impl_->device, size, static_cast<uint32_t>(count), &impl_->window_size, &impl_->window_count);
    if (ret != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("The driver could not provide {} DMA windows of {} bytes: {}", count, size, strerror(-ret)));
    }
    if (impl_->window_count < count) {
        log_warning(
            LogUMD,
            "Asked for {} DMA windows of {} bytes and got {}; batches will run in more chains.",
            count,
            size,
            impl_->window_count);
    }

    ret = io_uring_queue_init(RING_ENTRIES, &impl_->ring, 0);
    if (ret != 0) {
        tt_dma_set_windows(impl_->device, 0, 0, nullptr, nullptr);
        UMD_THROW(error::RuntimeError, fmt::format("Creating the DMA io_uring failed: {}", strerror(-ret)));
    }
}

DmaQueue::~DmaQueue() {
    try {
        while (!impl_->in_flight.empty()) {
            impl_->reap(true);
        }
    } catch (const std::exception& e) {
        log_warning(LogUMD, "DMA writes failed while closing the queue: {}", e.what());
    }
    io_uring_queue_exit(&impl_->ring);
    tt_dma_set_windows(impl_->device, 0, 0, nullptr, nullptr);
}

uint64_t DmaQueue::submit(const DmaWrite* writes, size_t count) {
    Impl::Batch& batch = impl_->in_flight.emplace_back();
    const uint64_t ticket = impl_->completed + impl_->in_flight.size();

    for (size_t i = 0; i < count; i++) {
        impl_->cut(writes[i], batch.descs);
    }

    // The driver takes a command only if it has a window for each of its regions, so a command ends where the next
    // descriptor would need one more.
    std::vector<Region> regions;
    size_t begin = 0;
    for (size_t i = 0; i < batch.descs.size(); i++) {
        const tt_dma_write_desc_t& desc = batch.descs[i];
        const Region region{desc.x, desc.y, desc.noc, desc.addr & ~(impl_->window_size - 1)};
        const bool known = std::find(regions.begin(), regions.end(), region) != regions.end();

        if ((!known && regions.size() == impl_->window_count) || i - begin == TT_DMA_WRITE_MAX_DESCS) {
            impl_->queue_command(&batch.descs[begin], i - begin, ticket);
            batch.commands_in_flight++;
            regions.clear();
            begin = i;
        }
        if (!known || regions.empty()) {
            regions.push_back(region);
        }
    }
    if (begin < batch.descs.size()) {
        impl_->queue_command(&batch.descs[begin], batch.descs.size() - begin, ticket);
        batch.commands_in_flight++;
    }

    impl_->flush();
    return ticket;
}

uint64_t DmaQueue::completed() {
    impl_->reap(false);
    return impl_->completed;
}

void DmaQueue::wait(uint64_t ticket) {
    UMD_ASSERT(
        ticket <= impl_->completed + impl_->in_flight.size(),
        error::RuntimeError,
        fmt::format("No DMA batch with ticket {} was submitted.", ticket));
    impl_->reap(false);
    while (impl_->completed < ticket) {
        impl_->reap(true);
    }
}

uint64_t DmaQueue::window_size() const { return impl_->window_size; }

uint32_t DmaQueue::window_count() const { return impl_->window_count; }

}  // namespace tt::umd
