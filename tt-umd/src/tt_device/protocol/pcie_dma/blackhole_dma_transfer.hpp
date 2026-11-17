// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "tt-umd-utils/mutex_interface.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt_device/protocol/dma_interface.hpp"

namespace tt::umd {

class PCIDevice;
class TlbWindow;

/**
 * @brief DmaInterface over Blackhole's PCIe DMA engine.
 *
 * Device-to-Host zero-copy transfers are not supported on Blackhole and throw.
 *
 * Every transfer, blocking or asynchronous, holds the device's PCIE_DMA lock while it runs, so transfers from other
 * threads and processes wait for it. An asynchronous transfer keeps holding it until its starting thread sees it
 * finish.
 *
 * The PCIDevice is non-owning and must outlive this object.
 */
class BlackholeDmaTransfer : public DmaInterface {
public:
    explicit BlackholeDmaTransfer(PCIDevice* pci_device);
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
    // Core, or core range for a multicast, that a transfer writes to.
    struct Destination {
        tt_xy_pair core_start;
        std::optional<tt_xy_pair> core_end;
        NocId noc_id;
    };

    // The asynchronous transfer in flight, owned by the thread that started it.
    struct AsyncTransfer {
        std::unique_lock<MutexInterface> device_lock;
        std::thread::id owner;
        uint16_t token;
        std::chrono::steady_clock::time_point deadline;
    };

    // How a transfer ended, as written by the engine to the completion page.
    enum class Completion { PENDING, DONE, ABORTED };

    // Whether this device has the pinned staging buffer and completion page every transfer needs.
    bool is_available() const;

    // Takes the device for a blocking transfer, waiting for any other transfer to end.
    std::unique_lock<MutexInterface> lock_for_blocking_transfer();

    // Runs a host-to-device transfer of any size under the device lock, split where the window ends.
    void write_blocking(const uint8_t* src, uint64_t src_iova, uint64_t dst_addr, size_t size, const Destination& dst);

    // Starts one transfer that fits the window at dst_addr. Returns the token its completion carries.
    uint16_t start_transfer(uint64_t src_iova, uint64_t dst_addr, size_t size, const Destination& dst);

    // Waits for a transfer started by start_transfer() and throws unless it completed.
    void wait_for_transfer(uint16_t token);

    Completion read_completion(uint16_t token) const;

    bool is_channel_running() const;

    // Waits until a transfer this object did not start, such as one left behind by a timed-out transfer, ends.
    void wait_for_channel_idle();

    uint32_t read_channel_register(uint32_t offset) const;
    void write_channel_register(uint32_t offset, uint32_t value);

    PCIDevice* pci_device_ = nullptr;

    // Window the engine writes the device through; made on first use. Used only under the device lock.
    std::unique_ptr<TlbWindow> window_;
    uint16_t last_token_ = 0;

    // Guards the bookkeeping below, which tells this process's threads who is transferring.
    std::mutex state_mutex_;
    std::optional<AsyncTransfer> async_transfer_;
    bool blocking_transfer_in_flight_ = false;
};

}  // namespace tt::umd
