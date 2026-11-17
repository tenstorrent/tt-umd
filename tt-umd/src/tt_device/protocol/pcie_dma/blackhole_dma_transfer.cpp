// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/lock_manager.hpp"
#include "tt-umd/utils/timeouts.hpp"

namespace tt::umd {

namespace {

// BAR2 maps the PCIe controller's HDMA registers. The engine's read channel 0 reads host memory and writes the
// device, which is the host-to-device direction. Register offsets within the channel block follow the firmware's
// PCIe DMA handler (fw/blackhole/pcie.md, "PCIe DMA messages"); BAR2 0x0 corresponds to its DBI offset 0x380000.
constexpr uint32_t H2D_CHANNEL_BASE = 0x100;
constexpr uint32_t CHANNEL_EN = 0x00;
constexpr uint32_t CHANNEL_DOORBELL = 0x04;
constexpr uint32_t CHANNEL_XFERSIZE = 0x1C;
constexpr uint32_t CHANNEL_SAR_LOW = 0x20;
constexpr uint32_t CHANNEL_SAR_HIGH = 0x24;
constexpr uint32_t CHANNEL_DAR_LOW = 0x28;
constexpr uint32_t CHANNEL_DAR_HIGH = 0x2C;
constexpr uint32_t CHANNEL_STATUS = 0x80;
constexpr uint32_t CHANNEL_INT_SETUP = 0x88;
constexpr uint32_t CHANNEL_MSI_STOP_LOW = 0x90;
constexpr uint32_t CHANNEL_MSI_STOP_HIGH = 0x94;
constexpr uint32_t CHANNEL_MSI_ABORT_LOW = 0xA0;
constexpr uint32_t CHANNEL_MSI_ABORT_HIGH = 0xA4;
constexpr uint32_t CHANNEL_MSI_MSGD = 0xA8;
constexpr uint32_t CHANNEL_REGISTERS_END = H2D_CHANNEL_BASE + 0x100;

constexpr uint32_t CHANNEL_STATUS_RUNNING = 1;
// Remote stop and remote abort interrupts: the engine writes the message data to the MSI stop address when the
// transfer completes and to the MSI abort address when it aborts.
constexpr uint32_t INT_SETUP_REMOTE_STOP_AND_ABORT = 0x28;

// The engine writes the device through a TLB window: BAR0 maps onto the controller's AXI space from address 0, so a
// window's BAR0 offset is also the address the engine writes to. A transfer cannot run past the window's end.
constexpr size_t WINDOW_SIZE = 2 * 1024 * 1024;

// The completion page holds the word the engine writes on completion, followed by the one it writes on abort.
constexpr size_t COMPLETION_WORDS = 2;

// A transfer of at most one window takes well under a millisecond, so waiters spin through it before sleeping.
constexpr auto BUSY_POLL_WINDOW = std::chrono::microseconds(1000);
constexpr auto POLL_INTERVAL = std::chrono::microseconds(50);

// The engine is only given transfers of whole 32-bit words at word-aligned device addresses. Bounce-buffer writes that
// do not fit this fall back to MMIO; zero-copy transfers have no fallback and throw.
bool is_word_aligned(uint64_t device_addr, size_t size) {
    return size % sizeof(uint32_t) == 0 && device_addr % sizeof(uint32_t) == 0;
}

void validate_word_aligned(uint64_t device_addr, size_t size) {
    if (!is_word_aligned(device_addr, size)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "DMA transfer of {} bytes at device address {:#x} must be a multiple of 4 bytes at a 4-byte aligned "
                "address.",
                size,
                device_addr));
    }
}

void validate_async_transfer(uint64_t device_addr, size_t size, size_t max_size) {
    if (size == 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Asynchronous DMA transfer at device address {:#x} must not be empty.", device_addr));
    }
    validate_word_aligned(device_addr, size);
    if (size > max_size) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "DMA transfer of {} bytes at device address {:#x} exceeds the {}-byte maximum for that address.",
                size,
                device_addr,
                max_size));
    }
}

}  // namespace

BlackholeDmaTransfer::BlackholeDmaTransfer(PCIDevice* pci_device) : pci_device_(pci_device) {
    LockManager::initialize_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
}

BlackholeDmaTransfer::~BlackholeDmaTransfer() {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    if (!async_transfer_.has_value()) {
        return;
    }
    // The owner never saw its transfer finish. Let the engine finish with the window and the host memory before
    // both go away, then try to give the device lock back.
    utils::poll_until([&] { return !is_channel_running(); }, timeout::DMA_TIMEOUT, BUSY_POLL_WINDOW, POLL_INTERVAL);
    std::unique_lock<MutexInterface>& device_lock = async_transfer_->device_lock;
    try {
        device_lock.unlock();
    } catch (const std::exception& e) {
        // Typically this runs on a thread other than the one that started the transfer, which cannot give the lock
        // back. The unique_lock must not keep owning it, or its destructor would unlock it again and terminate.
        device_lock.release();
        log_warning(
            LogUMD,
            "An unchecked asynchronous PCIe DMA on device {} was abandoned; its PCIe DMA lock stays held until the "
            "thread that started it exits: {}",
            pci_device_->get_device_num(),
            e.what());
    }
}

bool BlackholeDmaTransfer::dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    throw_if_async_owner();
    // The engine's device-to-host direction is not supported on Blackhole; the caller reads over MMIO instead.
    return false;
}

bool BlackholeDmaTransfer::dma_write(const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    throw_if_async_owner();
    if (!is_available() || !is_word_aligned(dst_addr, size)) {
        return false;
    }
    write_blocking(static_cast<const uint8_t*>(src), 0, dst_addr, size, Destination{core, std::nullopt, noc_id});
    return true;
}

bool BlackholeDmaTransfer::dma_multicast_write(
    const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    throw_if_async_owner();
    if (!is_available() || !is_word_aligned(dst_addr, size)) {
        return false;
    }
    write_blocking(static_cast<const uint8_t*>(src), 0, dst_addr, size, Destination{core_start, core_end, noc_id});
    return true;
}

bool BlackholeDmaTransfer::dma_read_zero_copy(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    throw_if_async_owner();
    return false;
}

bool BlackholeDmaTransfer::dma_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    throw_if_async_owner();
    if (!is_available()) {
        return false;
    }
    validate_word_aligned(dst_addr, size);
    write_blocking(nullptr, src_iova, dst_addr, size, Destination{core, std::nullopt, noc_id});
    return true;
}

bool BlackholeDmaTransfer::dma_multicast_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    throw_if_async_owner();
    if (!is_available()) {
        return false;
    }
    validate_word_aligned(dst_addr, size);
    write_blocking(nullptr, src_iova, dst_addr, size, Destination{core_start, core_end, noc_id});
    return true;
}

bool BlackholeDmaTransfer::dma_write_zero_copy_start(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    validate_async_transfer(dst_addr, size, dma_zero_copy_max_size(dst_addr));
    if (!is_available()) {
        UMD_THROW(error::RuntimeError, "PCIe DMA is not available on this device: it has no pinned DMA buffer.");
    }

    std::lock_guard<std::mutex> state_lock(state_mutex_);
    if (async_transfer_.has_value() || blocking_transfer_in_flight_) {
        return false;
    }
    std::unique_lock<MutexInterface> device_lock =
        LockManager::try_acquire_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
    // Someone outside the lock, such as a firmware DMA request, may also have the engine busy.
    if (!device_lock.owns_lock() || is_channel_running()) {
        return false;
    }
    const uint16_t token = start_transfer(src_iova, dst_addr, size, Destination{core, std::nullopt, noc_id});
    async_transfer_.emplace(AsyncTransfer{
        std::move(device_lock),
        std::this_thread::get_id(),
        token,
        std::chrono::steady_clock::now() + timeout::DMA_TIMEOUT});
    return true;
}

bool BlackholeDmaTransfer::dma_read_zero_copy_start(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "Device-to-host DMA is not supported on Blackhole.");
}

DmaTransferState BlackholeDmaTransfer::dma_write_zero_copy_check() {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    if (!async_transfer_.has_value()) {
        return blocking_transfer_in_flight_ ? DmaTransferState::IN_PROGRESS : DmaTransferState::IDLE;
    }
    if (async_transfer_->owner != std::this_thread::get_id()) {
        return DmaTransferState::IN_PROGRESS;
    }

    const Completion completion = read_completion(async_transfer_->token);
    if (completion == Completion::PENDING && std::chrono::steady_clock::now() < async_transfer_->deadline) {
        return DmaTransferState::IN_PROGRESS;
    }
    if (completion != Completion::DONE) {
        log_warning(
            LogUMD,
            "Asynchronous PCIe DMA on device {} {}.",
            pci_device_->get_device_num(),
            completion == Completion::ABORTED ? "was aborted" : "did not complete in time");
    }
    // Releases the device lock, from the thread that took it.
    async_transfer_.reset();
    return completion == Completion::DONE ? DmaTransferState::FINISHED : DmaTransferState::ERROR;
}

DmaTransferState BlackholeDmaTransfer::dma_read_zero_copy_check() {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    return blocking_transfer_in_flight_ ? DmaTransferState::IN_PROGRESS : DmaTransferState::IDLE;
}

size_t BlackholeDmaTransfer::dma_zero_copy_max_size(uint64_t device_addr) const {
    return WINDOW_SIZE - device_addr % WINDOW_SIZE;
}

bool BlackholeDmaTransfer::is_available() const {
    const DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
    return dma_buffer.buffer != nullptr && pci_device_->bar2_uc != nullptr &&
           pci_device_->bar2_uc_size >= CHANNEL_REGISTERS_END;
}

void BlackholeDmaTransfer::throw_if_async_owner() {
    std::lock_guard<std::mutex> state_lock(state_mutex_);
    if (async_transfer_.has_value() && async_transfer_->owner == std::this_thread::get_id()) {
        UMD_THROW(
            error::RuntimeError,
            "This thread has an asynchronous DMA transfer in flight; a blocking DMA from it would wait for itself. "
            "Check the transfer until it finishes first.");
    }
}

void BlackholeDmaTransfer::write_blocking(
    const uint8_t* src, uint64_t src_iova, uint64_t dst_addr, size_t size, const Destination& dst) {
    std::unique_lock<MutexInterface> device_lock =
        LockManager::acquire_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
    {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        blocking_transfer_in_flight_ = true;
    }

    try {
        wait_for_channel_idle();
        DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
        for (size_t offset = 0; offset < size;) {
            size_t chunk = std::min(size - offset, dma_zero_copy_max_size(dst_addr + offset));
            uint64_t chunk_iova = src_iova + offset;
            if (src != nullptr) {
                // Stage through the pinned buffer.
                chunk = std::min(chunk, dma_buffer.size);
                std::memcpy(dma_buffer.buffer, src + offset, chunk);
                chunk_iova = dma_buffer.buffer_pa;
            }
            wait_for_transfer(start_transfer(chunk_iova, dst_addr + offset, chunk, dst));
            offset += chunk;
        }
    } catch (...) {
        std::lock_guard<std::mutex> state_lock(state_mutex_);
        blocking_transfer_in_flight_ = false;
        throw;
    }

    std::lock_guard<std::mutex> state_lock(state_mutex_);
    blocking_transfer_in_flight_ = false;
}

uint16_t BlackholeDmaTransfer::start_transfer(
    uint64_t src_iova, uint64_t dst_addr, size_t size, const Destination& dst) {
    if (window_ == nullptr) {
        // The engine, not the host, is the next user of every configuration, so each one is confirmed on the device
        // before the engine is started.
        window_ = std::make_unique<SiliconTlbWindow>(
            pci_device_->allocate_tlb(WINDOW_SIZE, TlbMapping::UC, /*verify_config=*/true));
    }
    const WindowFlags direction = dst.core_end.has_value() ? WindowFlags::MulticastWrite : WindowFlags::UnicastWrite;
    // Default ordering acks each AXI write only after the NOC acks it, so the engine reports completion only once the
    // data has landed, at full speed. Strict ordering would serialize the writes for no added guarantee.
    window_->configure(
        TargetIoWindowConfig{dst.core_start, dst.core_end, dst_addr, dst.noc_id, direction}, IoOrdering::Relaxed);
    const uint64_t window_bar0_offset =
        get_architecture_tlbs(tt::ARCH::BLACKHOLE)
            .get_configuration(static_cast<uint32_t>(window_->handle_ref().get_tlb_id()))
            .tlb_offset;
    const uint64_t engine_dst_addr = window_bar0_offset + dst_addr % WINDOW_SIZE;

    // The message data the engine writes on completion lands in the low 16 bits of the completion word.
    last_token_ = last_token_ == UINT16_MAX ? 1 : last_token_ + 1;
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
    volatile uint32_t* completion = reinterpret_cast<volatile uint32_t*>(dma_buffer.completion);
    for (size_t i = 0; i < COMPLETION_WORDS; i++) {
        completion[i] = 0;
    }
    // The staged data and the cleared completion words must be visible before the doorbell starts the engine.
    std::atomic_thread_fence(std::memory_order_seq_cst);

    const uint64_t msi_stop_addr = dma_buffer.completion_pa;
    const uint64_t msi_abort_addr = dma_buffer.completion_pa + sizeof(uint32_t);
    write_channel_register(CHANNEL_EN, 1);
    write_channel_register(CHANNEL_INT_SETUP, INT_SETUP_REMOTE_STOP_AND_ABORT);
    write_channel_register(CHANNEL_MSI_STOP_LOW, static_cast<uint32_t>(msi_stop_addr));
    write_channel_register(CHANNEL_MSI_STOP_HIGH, static_cast<uint32_t>(msi_stop_addr >> 32));
    write_channel_register(CHANNEL_MSI_ABORT_LOW, static_cast<uint32_t>(msi_abort_addr));
    write_channel_register(CHANNEL_MSI_ABORT_HIGH, static_cast<uint32_t>(msi_abort_addr >> 32));
    write_channel_register(CHANNEL_MSI_MSGD, last_token_);
    write_channel_register(CHANNEL_XFERSIZE, static_cast<uint32_t>(size));
    write_channel_register(CHANNEL_SAR_LOW, static_cast<uint32_t>(src_iova));
    write_channel_register(CHANNEL_SAR_HIGH, static_cast<uint32_t>(src_iova >> 32));
    write_channel_register(CHANNEL_DAR_LOW, static_cast<uint32_t>(engine_dst_addr));
    write_channel_register(CHANNEL_DAR_HIGH, static_cast<uint32_t>(engine_dst_addr >> 32));
    write_channel_register(CHANNEL_DOORBELL, 1);
    log_trace(LogUMD, "PCIe DMA of {} bytes from {:#x} to {:#x} started.", size, src_iova, dst_addr);
    return last_token_;
}

void BlackholeDmaTransfer::wait_for_transfer(uint16_t token) {
    Completion completion = Completion::PENDING;
    utils::poll_until(
        [&] {
            completion = read_completion(token);
            return completion != Completion::PENDING;
        },
        timeout::DMA_TIMEOUT,
        BUSY_POLL_WINDOW,
        POLL_INTERVAL);
    if (completion != Completion::DONE) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "PCIe DMA on device {} {}.",
                pci_device_->get_device_num(),
                completion == Completion::ABORTED
                    ? "was aborted"
                    : fmt::format("did not complete within {} s", timeout::DMA_TIMEOUT.count())));
    }
}

BlackholeDmaTransfer::Completion BlackholeDmaTransfer::read_completion(uint16_t token) const {
    const volatile uint32_t* completion =
        reinterpret_cast<const volatile uint32_t*>(pci_device_->get_dma_buffer().completion);
    if (completion[0] == token) {
        return Completion::DONE;
    }
    if (completion[1] == token) {
        return Completion::ABORTED;
    }
    return Completion::PENDING;
}

bool BlackholeDmaTransfer::is_channel_running() const {
    return read_channel_register(CHANNEL_STATUS) == CHANNEL_STATUS_RUNNING;
}

void BlackholeDmaTransfer::wait_for_channel_idle() {
    if (!utils::poll_until(
            [&] { return !is_channel_running(); }, timeout::DMA_TIMEOUT, BUSY_POLL_WINDOW, POLL_INTERVAL)) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "The PCIe DMA engine of device {} stayed busy for {} s with an earlier transfer still running "
                "(the firmware's, or one of ours that timed out).",
                pci_device_->get_device_num(),
                timeout::DMA_TIMEOUT.count()));
    }
}

uint32_t BlackholeDmaTransfer::read_channel_register(uint32_t offset) const {
    return static_cast<const volatile uint32_t*>(pci_device_->bar2_uc)[(H2D_CHANNEL_BASE + offset) / sizeof(uint32_t)];
}

void BlackholeDmaTransfer::write_channel_register(uint32_t offset, uint32_t value) {
    static_cast<volatile uint32_t*>(pci_device_->bar2_uc)[(H2D_CHANNEL_BASE + offset) / sizeof(uint32_t)] = value;
}

}  // namespace tt::umd
