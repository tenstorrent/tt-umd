// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/lock_manager.hpp"
#include "tt-umd/utils/timeouts.hpp"

namespace tt::umd {

namespace {

// The engine's registers sit at the start of BAR2, one 0x200 byte block per channel: device-to-host at 0x0,
// host-to-device at 0x100. Channel 0 is the one used.
constexpr uint32_t HOST_TO_DEVICE_CHANNEL_OFFSET = 0x100;
constexpr uint32_t CHANNEL_REGISTERS_END = 0x200;

// Register offsets within a channel's block.
constexpr uint32_t CH_ENABLE = 0x00;
constexpr uint32_t CH_DOORBELL = 0x04;
constexpr uint32_t CH_TRANSFER_SIZE = 0x1C;
constexpr uint32_t CH_SOURCE_LOW = 0x20;
constexpr uint32_t CH_SOURCE_HIGH = 0x24;
constexpr uint32_t CH_DESTINATION_LOW = 0x28;
constexpr uint32_t CH_DESTINATION_HIGH = 0x2C;
constexpr uint32_t CH_CONTROL = 0x34;
constexpr uint32_t CH_STATUS = 0x80;

// CH_CONTROL value for a single transfer described by the channel registers themselves, not by a descriptor list.
constexpr uint32_t CONTROL_SINGLE_TRANSFER = 0;
constexpr uint32_t STATUS_MASK = 0x3;
constexpr uint32_t STATUS_RUNNING = 1;
constexpr uint32_t STATUS_STOPPED = 3;

constexpr auto DMA_POLL_TIMEOUT = std::chrono::duration_cast<std::chrono::milliseconds>(timeout::DMA_TIMEOUT);
// A 2 MiB chunk takes a few hundred microseconds, so the wait spins for about that long before backing off.
constexpr auto DMA_BUSY_POLL_WINDOW = std::chrono::microseconds(1000);
constexpr auto DMA_POLL_INTERVAL = std::chrono::microseconds(10);

[[noreturn]] void throw_device_to_host_unsupported() {
    UMD_THROW(error::RuntimeError, "Device-to-host DMA is not supported on Blackhole.");
}

}  // namespace

BlackholeDmaTransfer::BlockingLease::BlockingLease(
    BlackholeDmaTransfer& dma, std::unique_lock<MutexInterface> device_lock) :
    dma_(dma), device_lock_(std::move(device_lock)) {}

BlackholeDmaTransfer::BlockingLease::~BlockingLease() {
    device_lock_.unlock();
    dma_.release_device();
}

BlackholeDmaTransfer::BlackholeDmaTransfer(PCIDevice* pci_device) :
    pci_device_(pci_device),
    channel_(static_cast<volatile uint32_t*>(pci_device->bar2_uc) + HOST_TO_DEVICE_CHANNEL_OFFSET / sizeof(uint32_t)) {
    UMD_ASSERT(
        pci_device->bar2_uc != nullptr && pci_device->bar2_uc_size >= CHANNEL_REGISTERS_END,
        error::RuntimeError,
        fmt::format("PCI device {} does not expose the DMA engine's registers.", pci_device->get_device_num()));
    // The engine is not the host, so the window has to be live before the engine is started.
    window_ = std::make_unique<SiliconTlbWindow>(
        pci_device->allocate_tlb(blackhole::TLB_2M_SIZE, TlbMapping::UC, /*verify_config=*/true));
    LockManager::initialize_mutex(MutexType::PCIE_DMA, pci_device->get_device_num(), IODeviceType::PCIe);
}

BlackholeDmaTransfer::~BlackholeDmaTransfer() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!async_device_lock_.owns_lock()) {
        return;
    }
    if (!utils::poll_until(
            [&] { return engine_state() != EngineState::RUNNING; },
            DMA_POLL_TIMEOUT,
            DMA_BUSY_POLL_WINDOW,
            DMA_POLL_INTERVAL)) {
        log_warning(LogUMD, "An unfinished DMA transfer on device {} is still running.", pci_device_->get_device_num());
    }
    // The lock may belong to another thread, which a process-shared lock refuses to release for it.
    try {
        async_device_lock_.unlock();
    } catch (const std::exception& e) {
        log_warning(LogUMD, "Could not release the DMA lock of device {}: {}", pci_device_->get_device_num(), e.what());
        (void)async_device_lock_.release();
    }
}

bool BlackholeDmaTransfer::dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return false;
}

bool BlackholeDmaTransfer::dma_write(const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return bounce_write(src, size, Target{core, std::nullopt, dst_addr, noc_id});
}

bool BlackholeDmaTransfer::dma_multicast_write(
    const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    return bounce_write(src, size, Target{core_start, core_end, dst_addr, noc_id});
}

bool BlackholeDmaTransfer::dma_read_zero_copy(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return false;
}

bool BlackholeDmaTransfer::dma_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    BlockingLease lease = take_device();
    transfer(Target{core, std::nullopt, dst_addr, noc_id}, size, size, [&](size_t offset, size_t) {
        return src_iova + offset;
    });
    return true;
}

bool BlackholeDmaTransfer::dma_multicast_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    BlockingLease lease = take_device();
    transfer(Target{core_start, core_end, dst_addr, noc_id}, size, size, [&](size_t offset, size_t) {
        return src_iova + offset;
    });
    return true;
}

bool BlackholeDmaTransfer::dma_write_zero_copy_start(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    if (size == 0 || size % sizeof(uint32_t) != 0 || dst_addr % sizeof(uint32_t) != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "An asynchronous DMA transfer needs a non-zero size and a device address that are multiples of 4, got "
                "{:#x} bytes at {:#x}.",
                size,
                dst_addr));
    }
    const size_t max_size = dma_zero_copy_max_size(dst_addr);
    if (size > max_size) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "An asynchronous DMA transfer to {:#x} can move at most {:#x} bytes, got {:#x}.",
                dst_addr,
                max_size,
                size));
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    if (device_taken_) {
        return false;
    }
    std::unique_lock<MutexInterface> device_lock =
        LockManager::try_acquire_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
    if (!device_lock.owns_lock()) {
        return false;
    }
    start_engine(src_iova, map_window(Target{core, std::nullopt, dst_addr, noc_id}, dst_addr), size);
    device_taken_ = true;
    async_owner_ = std::this_thread::get_id();
    async_deadline_ = std::chrono::steady_clock::now() + timeout::DMA_TIMEOUT;
    async_device_lock_ = std::move(device_lock);
    return true;
}

bool BlackholeDmaTransfer::dma_read_zero_copy_start(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    throw_device_to_host_unsupported();
}

DmaTransferState BlackholeDmaTransfer::dma_write_zero_copy_check() {
    std::unique_lock<std::mutex> lock(state_mutex_);
    if (async_owner_ != std::this_thread::get_id()) {
        return device_taken_ ? DmaTransferState::IN_PROGRESS : DmaTransferState::IDLE;
    }

    const EngineState state = engine_state();
    if (state == EngineState::RUNNING && std::chrono::steady_clock::now() < async_deadline_) {
        return DmaTransferState::IN_PROGRESS;
    }
    if (state != EngineState::DONE) {
        log_error(
            LogUMD,
            "Asynchronous DMA transfer on device {} {}.",
            pci_device_->get_device_num(),
            state == EngineState::RUNNING ? "timed out" : "failed");
    }
    async_device_lock_.unlock();
    async_owner_.reset();
    device_taken_ = false;
    lock.unlock();
    device_released_.notify_all();
    return state == EngineState::DONE ? DmaTransferState::FINISHED : DmaTransferState::ERROR;
}

DmaTransferState BlackholeDmaTransfer::dma_read_zero_copy_check() {
    // Only a blocking transfer can occupy the device for this direction, since no device-to-host one ever starts.
    std::lock_guard<std::mutex> lock(state_mutex_);
    return device_taken_ && !async_owner_.has_value() ? DmaTransferState::IN_PROGRESS : DmaTransferState::IDLE;
}

size_t BlackholeDmaTransfer::dma_zero_copy_max_size(uint64_t device_addr) const {
    const uint64_t window_size = window_->handle_ref().get_size();
    return window_size - device_addr % window_size;
}

BlackholeDmaTransfer::BlockingLease BlackholeDmaTransfer::take_device() {
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        if (async_owner_ == std::this_thread::get_id()) {
            UMD_THROW(
                error::RuntimeError,
                "This thread has an asynchronous DMA transfer in flight, which has to be checked until it finishes "
                "before the thread starts a blocking one.");
        }
        device_released_.wait(lock, [&] { return !device_taken_; });
        device_taken_ = true;
    }
    try {
        return BlockingLease(
            *this, LockManager::acquire_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe));
    } catch (...) {
        release_device();
        throw;
    }
}

void BlackholeDmaTransfer::release_device() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        device_taken_ = false;
    }
    device_released_.notify_all();
}

void BlackholeDmaTransfer::transfer(const Target& target, size_t size, size_t max_chunk, const ChunkSource& source) {
    for (size_t offset = 0; offset < size;) {
        const uint64_t addr = target.addr + offset;
        const size_t chunk_size = std::min({size - offset, max_chunk, dma_zero_copy_max_size(addr)});
        const uint64_t destination = map_window(target, addr);
        start_engine(source(offset, chunk_size), destination, chunk_size);
        wait_for_engine();
        offset += chunk_size;
    }
}

bool BlackholeDmaTransfer::bounce_write(const void* src, size_t size, const Target& target) {
    BlockingLease lease = take_device();
    DmaBuffer& bounce_buffer = pci_device_->get_dma_buffer();
    if (bounce_buffer.buffer == nullptr) {
        return false;
    }
    transfer(target, size, bounce_buffer.size, [&](size_t offset, size_t chunk_size) {
        std::memcpy(bounce_buffer.buffer, static_cast<const uint8_t*>(src) + offset, chunk_size);
        return bounce_buffer.buffer_pa;
    });
    return true;
}

uint64_t BlackholeDmaTransfer::map_window(const Target& target, uint64_t addr) {
    TargetIoWindowConfig config{};
    config.core_start = target.core_start;
    config.core_end = target.core_end;
    config.addr = addr;
    config.noc = target.noc_id;
    config.flags = target.core_end.has_value() ? WindowFlags::MulticastWrite : WindowFlags::UnicastWrite;
    // Strict ordering makes every write acknowledged, so the engine reports a transfer done only once it has landed.
    window_->configure(config, IoOrdering::Strict);
    // The engine writes into the controller's own address space, where a 2 MiB window sits at its BAR0 offset.
    const TlbHandle& handle = window_->handle_ref();
    return blackhole::TLB_BASE_2M + static_cast<uint64_t>(handle.get_tlb_id()) * handle.get_size() +
           addr % handle.get_size();
}

void BlackholeDmaTransfer::start_engine(uint64_t src_iova, uint64_t destination, size_t size) {
    channel_[CH_ENABLE / sizeof(uint32_t)] = 1;
    channel_[CH_CONTROL / sizeof(uint32_t)] = CONTROL_SINGLE_TRANSFER;
    channel_[CH_TRANSFER_SIZE / sizeof(uint32_t)] = static_cast<uint32_t>(size);
    channel_[CH_SOURCE_LOW / sizeof(uint32_t)] = static_cast<uint32_t>(src_iova);
    channel_[CH_SOURCE_HIGH / sizeof(uint32_t)] = static_cast<uint32_t>(src_iova >> 32);
    channel_[CH_DESTINATION_LOW / sizeof(uint32_t)] = static_cast<uint32_t>(destination);
    channel_[CH_DESTINATION_HIGH / sizeof(uint32_t)] = static_cast<uint32_t>(destination >> 32);
    // Whatever the host wrote to the source memory has to be there before the engine reads it.
    std::atomic_thread_fence(std::memory_order_release);
    channel_[CH_DOORBELL / sizeof(uint32_t)] = 1;
}

BlackholeDmaTransfer::EngineState BlackholeDmaTransfer::engine_state() const {
    const uint32_t status = channel_[CH_STATUS / sizeof(uint32_t)] & STATUS_MASK;
    if (status == STATUS_RUNNING) {
        return EngineState::RUNNING;
    }
    // A channel that stopped short of the whole transfer was aborted.
    const bool all_moved = channel_[CH_TRANSFER_SIZE / sizeof(uint32_t)] == 0;
    return status == STATUS_STOPPED && all_moved ? EngineState::DONE : EngineState::FAILED;
}

void BlackholeDmaTransfer::wait_for_engine() const {
    EngineState state = EngineState::RUNNING;
    utils::poll_until(
        [&] {
            state = engine_state();
            return state != EngineState::RUNNING;
        },
        DMA_POLL_TIMEOUT,
        DMA_BUSY_POLL_WINDOW,
        DMA_POLL_INTERVAL);
    if (state != EngineState::DONE) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "DMA transfer on device {} {}.",
                pci_device_->get_device_num(),
                state == EngineState::RUNNING ? "timed out" : "failed"));
    }
}

}  // namespace tt::umd
