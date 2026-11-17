// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "tt-umd-utils/mutex_interface.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt_device/protocol/dma_interface.hpp"

namespace tt::umd {

/**
 * @brief Host-to-device DMA on a Blackhole chip, run by the DMA engine of its PCIe controller.
 *
 * The engine reads host memory and writes it through a TLB window of its own, so a transfer lands on whichever core
 * or core range that window targets. A transfer never crosses a window, which is what bounds
 * dma_zero_copy_max_size().
 *
 * Device-to-host DMA is not supported on Blackhole: the bounce-buffer and blocking zero-copy reads report DMA as
 * unavailable, and dma_read_zero_copy_start() throws.
 *
 * The device runs one transfer at a time, across every thread and process using it. Blocking calls wait for the
 * device; an asynchronous start does not, and returns false while it is taken.
 */
class BlackholeDmaTransfer : public DmaInterface {
public:
    // pci_device must expose BAR2 and outlive this object.
    explicit BlackholeDmaTransfer(PCIDevice* pci_device);

    // Waits for an asynchronous transfer whose owner never saw it finish, so the engine is not left writing through a
    // window that is about to be released.
    ~BlackholeDmaTransfer() override;

    [[nodiscard]] bool dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_multicast_write(
        const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    bool dma_read_zero_copy(uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    bool dma_write_zero_copy(uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    bool dma_multicast_write_zero_copy(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id)
        override;
    [[nodiscard]] bool dma_write_zero_copy_start(
        uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] bool dma_read_zero_copy_start(
        uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) override;
    [[nodiscard]] DmaTransferState dma_write_zero_copy_check() override;
    [[nodiscard]] DmaTransferState dma_read_zero_copy_check() override;
    [[nodiscard]] size_t dma_zero_copy_max_size(uint64_t device_addr) const override;

private:
    // Where a transfer lands: one core, or the core range from core_start to core_end.
    struct Target {
        tt_xy_pair core_start;
        std::optional<tt_xy_pair> core_end;
        uint64_t addr;
        NocId noc_id;
    };

    // Returns the IOVA holding the chunk_size bytes at offset into the transfer, ready for the engine to read.
    using ChunkSource = std::function<uint64_t(size_t offset, size_t chunk_size)>;

    // Holds the device for one blocking call: released, and waiting threads woken, when it goes out of scope.
    class BlockingLease {
    public:
        BlockingLease(BlackholeDmaTransfer& dma, std::unique_lock<MutexInterface> device_lock);
        ~BlockingLease();

        BlockingLease(const BlockingLease&) = delete;
        BlockingLease& operator=(const BlockingLease&) = delete;

    private:
        BlackholeDmaTransfer& dma_;
        std::unique_lock<MutexInterface> device_lock_;
    };

    enum class EngineState { RUNNING, DONE, FAILED };

    // Waits until neither another thread of this process nor another process uses the device. Throws if the calling
    // thread owns an asynchronous transfer, which would never finish while it waits.
    BlockingLease take_device();
    void release_device();

    // Moves size bytes in chunks no larger than max_chunk, each started only after the previous one finished.
    void transfer(const Target& target, size_t size, size_t max_chunk, const ChunkSource& source);
    bool bounce_write(const void* src, size_t size, const Target& target);

    // Points the window at target's address addr and returns where the engine has to write to reach it.
    uint64_t map_window(const Target& target, uint64_t addr);
    void start_engine(uint64_t src_iova, uint64_t destination, size_t size);
    EngineState engine_state() const;
    // Throws if the transfer fails or does not finish within the DMA timeout.
    void wait_for_engine() const;

    PCIDevice* pci_device_;
    // Registers of the engine's host-to-device channel.
    volatile uint32_t* channel_;
    // Configured only while the device is taken.
    std::unique_ptr<TlbWindow> window_;

    // Guards everything below, which tells who has the device within this process.
    std::mutex state_mutex_;
    std::condition_variable device_released_;
    bool device_taken_ = false;
    std::optional<std::thread::id> async_owner_;
    std::chrono::steady_clock::time_point async_deadline_;
    // Keeps other processes off the device for as long as an asynchronous transfer is in flight.
    std::unique_lock<MutexInterface> async_device_lock_;
};

}  // namespace tt::umd
