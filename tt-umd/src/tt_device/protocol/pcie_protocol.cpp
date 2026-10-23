/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tt_device/protocol/pcie_protocol.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <variant>
#include <vector>

#include "pcie/io_window_reconfigure.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/power_state.hpp"
#include "tt-umd/types/tlb.hpp"
#include "tt-umd/utils/timeouts.hpp"
#include "tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp"
#include "tt_device/protocol/pcie_dma/wormhole_dma_transfer.hpp"
#include "utils.hpp"

namespace tt::umd {

DmaTransferStrategy PcieProtocol::create_dma_strategy(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return WormholeDmaTransfer{};
        case tt::ARCH::BLACKHOLE:
            return BlackholeDmaTransfer{};
        default:
            UMD_THROW(error::RuntimeError, "Unsupported architecture for DMA transfer strategy.");
    }
}

size_t PcieProtocol::get_dma_tlb_size(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            return 2 * 1024 * 1024;
        case tt::ARCH::WORMHOLE_B0:
            return 16 * 1024 * 1024;
        default:
            UMD_THROW(error::RuntimeError, "Unsupported architecture for DMA TLB size.");
    }
}

PcieProtocol::PcieProtocol(std::unique_ptr<PCIDevice> pci_device, bool use_safe_api) :
    pci_device_(std::move(pci_device)),
    dma_strategy_(create_dma_strategy(pci_device_->get_arch())),
    use_safe_api_(use_safe_api) {
    LockManager::initialize_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
}

PcieProtocol::~PcieProtocol() {
    if (!in_flight_dma_.has_value()) {
        return;
    }
    // The engine writes its completion into memory freed with this object, so the transfer is let finish.
    log_warning(
        LogUMD, "PCI device {} closed with a DMA transfer in flight; waiting for it.", pci_device_->get_device_num());
    try {
        while (!dma_is_complete(in_flight_dma_->direction) &&
               std::chrono::steady_clock::now() < in_flight_dma_->deadline) {
        }
        in_flight_dma_->pcie_dma_lock.unlock();
    } catch (...) {
        // The shared memory half of the lock refuses a release from a thread other than the owner. It is
        // recovered once the owner thread exits.
        in_flight_dma_->pcie_dma_lock.release();
    }
}

void PcieProtocol::set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) {
    hang_check_ = hang_check;
    // The cached window may already exist if I/O ran before the hang detector was wired; keep it in sync.
    if (cached_tlb_window_ != nullptr) {
        cached_tlb_window_->set_io_timeout_hang_check(hang_check_);
    }
}

TlbWindow* PcieProtocol::get_cached_tlb_window() {
    if (cached_tlb_window_ == nullptr) {
        cached_tlb_window_ = std::make_unique<SiliconTlbWindow>(
            pci_device_->allocate_tlb(
                get_architecture_tlbs(pci_device_->get_arch()).cached_window_size, TlbMapping::UC),
            tlb_data{},
            static_cast<IoSafety>(use_safe_api_));
        cached_tlb_window_->set_io_timeout_hang_check(hang_check_);
    }
    return cached_tlb_window_.get();
}

void PcieProtocol::write_data(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    // The cached window carries the per-op MMIO timeout veto (built from its configured NOC + the wired
    // hang check) and the installed safe-I/O policy; see SiliconTlbWindow. The reconfigure call sets the
    // NOC before the transfer runs.
    std::lock_guard<std::mutex> lock(io_lock_);
    write_block_reconfigure(*get_cached_tlb_window(), mem_ptr, core, addr, size, noc_id);
}

void PcieProtocol::read_data(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(io_lock_);
    read_block_reconfigure(*get_cached_tlb_window(), mem_ptr, core, addr, size, noc_id);
}

void PcieProtocol::write_ctrl(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access(addr, size, width);
    std::lock_guard<std::mutex> lock(io_lock_);
    write_register_reconfigure(*get_cached_tlb_window(), mem_ptr, core, addr, size, noc_id, width);
}

void PcieProtocol::read_ctrl(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access(addr, size, width);
    std::lock_guard<std::mutex> lock(io_lock_);
    read_register_reconfigure(*get_cached_tlb_window(), mem_ptr, core, addr, size, noc_id, width);
}

bool PcieProtocol::write_to_core_range(
    const void* mem_ptr, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) {
    noc_multicast_write(mem_ptr, size, core_start, core_end, addr, noc_id);
    return true;
}

int PcieProtocol::get_mmio_id() { return pci_device_->get_device_num(); }

// PCIDevice takes a bool, so the requested state is narrowed here rather than in the interface.
void PcieProtocol::set_power_state(PowerState state) {
    pci_device_->set_power_state(/*busy=*/state == PowerState::HIGH);
}

void PcieProtocol::noc_multicast_write(
    const void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) {
    std::lock_guard<std::mutex> lock(io_lock_);
    noc_multicast_write_reconfigure(*get_cached_tlb_window(), src, size, core_start, core_end, addr, noc_id);
}

void PcieProtocol::bar_write32(uint32_t addr, uint32_t data) {
    if (addr < BAR0_OFFSET) {
        UMD_THROW(error::RuntimeError, "Write Invalid BAR address for this device.");
    }
    addr -= BAR0_OFFSET;
    *reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(pci_device_->bar0) + addr) = data;
}

uint32_t PcieProtocol::bar_read32(uint32_t addr) {
    if (addr < BAR0_OFFSET) {
        UMD_THROW(error::RuntimeError, "Read Invalid BAR address for this device.");
    }
    addr -= BAR0_OFFSET;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(pci_device_->bar0) + addr);
}

int PcieProtocol::get_numa_node() const { return pci_device_->get_numa_node(); }

std::optional<uint32_t> PcieProtocol::get_lane_count() const { return pci_device_->get_current_link_width(); }

// A TLB window's NOC base must be size-aligned, so the window aimed at addr sits at the size-aligned
// address at or below addr; the smallest window that still covers [addr, addr + size) is selected.
// The offset handed to the driver is addr's distance from that base, and both offset and size must
// be page-aligned.
int PcieProtocol::export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    // Nothing can consume this fd without an active RDMA NIC on the host, so check for one before
    // any window is allocated or an ioctl issued, same as the alignment checks below.
    UMD_ASSERT(
        tt::umd::utils::has_any_active_rdma_port(),
        error::RuntimeError,
        "Exporting a TLB window as a dma-buf requires an active RDMA NIC (RoCE or InfiniBand) on this host to "
        "consume it, but no port in the ACTIVE state was found under /sys/class/infiniband.");

    UMD_ASSERT(
        ordering == tlb_data::Strict || ordering == tlb_data::Posted || ordering == tlb_data::Relaxed,
        error::RuntimeError,
        "Invalid ordering specified in PcieProtocol::export_dmabuf");

    const uint64_t page_size = static_cast<uint64_t>(getpagesize());
    UMD_ASSERT(size != 0, error::RuntimeError, "Cannot export a dma-buf of size 0.");
    UMD_ASSERT(
        addr % page_size == 0,
        error::RuntimeError,
        fmt::format("Address {:#x} must be aligned to the host page size ({} bytes) to be exported.", addr, page_size));
    UMD_ASSERT(
        size % page_size == 0,
        error::RuntimeError,
        fmt::format("Size {} must be a multiple of the host page size ({} bytes) to be exported.", size, page_size));

    const std::vector<TlbSizeClass>& size_classes = get_architecture_tlbs(pci_device_->get_arch()).size_classes;
    size_t window_size = 0;
    for (const TlbSizeClass& size_class : size_classes) {
        if (size <= size_class.size - (addr % size_class.size)) {
            window_size = size_class.size;
            break;
        }
    }
    UMD_ASSERT(
        window_size != 0,
        error::RuntimeError,
        fmt::format(
            "No TLB window size can cover {} bytes at address {:#x}; the largest is {} bytes and its base must be "
            "size-aligned. Use a smaller size or a more aligned address.",
            size,
            addr,
            size_classes.back().size));

    const uint64_t window_offset = addr % window_size;

    tlb_data config{};
    config.local_offset = addr - window_offset;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = static_cast<uint64_t>(noc_id);
    config.ordering = ordering;
    config.static_vc = get_architecture_tlbs(pci_device_->get_arch()).use_static_vc;

    log_debug(
        LogUMD,
        "Exporting {} bytes at {:#x} on core {} as a dma-buf via a {} byte TLB window (offset {:#x} into the window).",
        size,
        addr,
        core.str(),
        window_size,
        window_offset);

    // Dedicated window (not a cached one): must not alias a window other traffic could reconfigure
    // while the export is live.
    return pci_device_->export_tlb_dmabuf(window_size, config, window_offset, size);
}

bool PcieProtocol::dma_write(const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    // const_cast is safe here: dma_transfer only reads from the buffer in H2D direction (memcpy into DMA buffer).
    // dma_transfer uses void* to handle both H2D (read) and D2H (write) in a single function.
    // TODO: Split dma_transfer into separate H2D/D2H functions to remove this cast.
    return dma_transfer(
        const_cast<void*>(src),  // NOLINT
        size,
        dst_addr,
        create_dma_tlb_config(dst_addr, core, noc_id, WindowFlags::UnicastWrite),
        DmaDirection::H2D);
}

bool PcieProtocol::dma_read(void* dst, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return dma_transfer(
        dst,
        size,
        src_addr,
        create_dma_tlb_config(src_addr, core, noc_id, WindowFlags::UnicastRead),
        DmaDirection::D2H);
}

bool PcieProtocol::dma_multicast_write(
    const void* src, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    return dma_transfer(
        const_cast<void*>(src),  // NOLINT
        size,
        dst_addr,
        create_dma_tlb_config(dst_addr, core_end, noc_id, WindowFlags::MulticastWrite, core_start),
        DmaDirection::H2D);
}

bool PcieProtocol::dma_read_zero_copy(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return dma_transfer_zero_copy(
        dst_iova,
        size,
        src_addr,
        create_dma_tlb_config(src_addr, core, noc_id, WindowFlags::UnicastRead),
        DmaDirection::D2H);
}

bool PcieProtocol::dma_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return dma_transfer_zero_copy(
        src_iova,
        size,
        dst_addr,
        create_dma_tlb_config(dst_addr, core, noc_id, WindowFlags::UnicastWrite),
        DmaDirection::H2D);
}

bool PcieProtocol::dma_multicast_write_zero_copy(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, NocId noc_id) {
    return dma_transfer_zero_copy(
        src_iova,
        size,
        dst_addr,
        create_dma_tlb_config(dst_addr, core_end, noc_id, WindowFlags::MulticastWrite, core_start),
        DmaDirection::H2D);
}

bool PcieProtocol::dma_write_zero_copy_start(
    uint64_t src_iova, uint64_t dst_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return dma_zero_copy_start(
        src_iova,
        size,
        dst_addr,
        create_dma_tlb_config(dst_addr, core, noc_id, WindowFlags::UnicastWrite),
        DmaDirection::H2D);
}

bool PcieProtocol::dma_read_zero_copy_start(
    uint64_t dst_iova, uint64_t src_addr, size_t size, tt_xy_pair core, NocId noc_id) {
    return dma_zero_copy_start(
        dst_iova,
        size,
        src_addr,
        create_dma_tlb_config(src_addr, core, noc_id, WindowFlags::UnicastRead),
        DmaDirection::D2H);
}

DmaTransferState PcieProtocol::dma_write_zero_copy_check() { return dma_zero_copy_check(DmaDirection::H2D); }

DmaTransferState PcieProtocol::dma_read_zero_copy_check() { return dma_zero_copy_check(DmaDirection::D2H); }

// Creates a TLB config for DMA transfers. Parameters are named core_end/core_start to match
// the x_end/y_end and x_start/y_start fields in tlb_data. For unicast, only core_end is needed
// (the target core). When core_start is provided, the transfer becomes a multicast to the
// core range [core_start, core_end].
tlb_data PcieProtocol::create_dma_tlb_config(
    uint64_t addr, tt_xy_pair core_end, NocId noc_id, WindowFlags flags, std::optional<tt_xy_pair> core_start) {
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.noc_sel = static_cast<uint64_t>(noc_id);
    config.ordering = tlb_data::Relaxed;
    config.set_static_vc(get_architecture_tlbs(pci_device_->get_arch()).get_static_vc(flags));
    if (core_start) {
        config.x_start = core_start->x;
        config.y_start = core_start->y;
        config.mcast = true;
    }
    return config;
}

bool PcieProtocol::dma_transfer(void* buffer, size_t size, uint64_t addr, tlb_data config, DmaDirection direction) {
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();

    if (dma_buffer.buffer == nullptr) {
        log_warning(LogUMD, "DMA buffer was not allocated for PCI device {}.", pci_device_->get_device_num());
        return false;
    }

    // After the availability check, so a caller falling back to MMIO does not wait on other threads or processes.
    auto [lock, pcie_dma_lock] = lock_dma_for_blocking_transfer();

    uint8_t* buf = static_cast<uint8_t*>(buffer);
    const size_t dmabuf_size = dma_buffer.size;
    TlbWindow* tlb_window = get_cached_dma_tlb_window(config);

    const auto tlb_window_addr_base = get_architecture_tlbs(pci_device_->get_arch())
                                          .get_configuration(tlb_window->handle_ref().get_tlb_id())
                                          .tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();

    while (size > 0) {
        config.local_offset = addr;
        tlb_window->configure(config);

        const auto tlb_window_addr = tlb_window_addr_base + (addr & (tlb_handle_size - 1));
        const size_t transfer_size = std::min({size, tlb_window->get_size(), dmabuf_size});

        if (direction == DmaDirection::H2D) {
            std::memcpy(dma_buffer.buffer, buf, transfer_size);
        }
        run_dma_transfer(direction, dma_buffer.buffer_pa, static_cast<uint32_t>(tlb_window_addr), transfer_size);
        if (direction == DmaDirection::D2H) {
            std::memcpy(buf, dma_buffer.buffer, transfer_size);
        }

        size -= transfer_size;
        addr += transfer_size;
        buf += transfer_size;
    }

    return true;
}

bool PcieProtocol::dma_transfer_zero_copy(
    uint64_t iova, size_t size, uint64_t addr, tlb_data config, DmaDirection direction) {
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();

    if (dma_buffer.buffer == nullptr) {
        log_warning(LogUMD, "DMA buffer was not allocated for PCI device {}.", pci_device_->get_device_num());
        return false;
    }

    auto [lock, pcie_dma_lock] = lock_dma_for_blocking_transfer();

    TlbWindow* tlb_window = get_cached_dma_tlb_window(config);

    const auto tlb_window_addr_base = get_architecture_tlbs(pci_device_->get_arch())
                                          .get_configuration(tlb_window->handle_ref().get_tlb_id())
                                          .tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();

    while (size > 0) {
        config.local_offset = addr;
        tlb_window->configure(config);

        const auto tlb_window_addr = tlb_window_addr_base + (addr & (tlb_handle_size - 1));
        const size_t transfer_size = std::min(size, tlb_window->get_size());

        run_dma_transfer(direction, iova, static_cast<uint32_t>(tlb_window_addr), transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        iova += transfer_size;
    }

    return true;
}

bool PcieProtocol::dma_zero_copy_start(
    uint64_t iova, size_t size, uint64_t addr, tlb_data config, DmaDirection direction) {
    if (pci_device_->get_dma_buffer().buffer == nullptr) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "DMA is not available: no DMA buffer was allocated for PCI device {}.", pci_device_->get_device_num()));
    }
    const size_t window_size = get_dma_tlb_size(pci_device_->get_arch());
    if (size == 0 || size % 4 != 0 || addr % 4 != 0 || (addr % window_size) + size > window_size) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "DMA transfer of {} bytes at device address {:#x} must be non-empty, have its size and address "
                "multiples of 4, and not cross a {} byte DMA window boundary.",
                size,
                addr,
                window_size));
    }

    // A blocking transfer holds dma_mutex_ for as long as it runs, so the device counts as busy then too.
    std::unique_lock lock(dma_mutex_, std::try_to_lock);
    if (!lock.owns_lock() || in_flight_dma_.has_value()) {
        return false;
    }
    auto pcie_dma_lock =
        LockManager::try_acquire_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
    if (!pcie_dma_lock.owns_lock()) {
        return false;
    }

    config.local_offset = addr;
    TlbWindow* tlb_window = get_cached_dma_tlb_window(config);
    tlb_window->configure(config);
    const uint64_t tlb_window_addr = get_architecture_tlbs(pci_device_->get_arch())
                                         .get_configuration(tlb_window->handle_ref().get_tlb_id())
                                         .tlb_offset +
                                     (addr & (tlb_window->handle_ref().get_size() - 1));
    program_dma_engine(direction, iova, static_cast<uint32_t>(tlb_window_addr), size);

    in_flight_dma_ = InFlightDma{
        std::this_thread::get_id(),
        direction,
        std::chrono::steady_clock::now() + timeout::DMA_TIMEOUT,
        std::move(pcie_dma_lock)};
    return true;
}

DmaTransferState PcieProtocol::dma_zero_copy_check(DmaDirection direction) {
    std::scoped_lock lock(dma_mutex_);
    if (!in_flight_dma_.has_value() || in_flight_dma_->direction != direction) {
        return DmaTransferState::IDLE;
    }
    if (in_flight_dma_->owner != std::this_thread::get_id()) {
        return DmaTransferState::IN_PROGRESS;
    }

    if (dma_is_complete(direction)) {
        in_flight_dma_.reset();
        return DmaTransferState::FINISHED;
    }
    if (std::chrono::steady_clock::now() <= in_flight_dma_->deadline) {
        return DmaTransferState::IN_PROGRESS;
    }
    // The engine may still be running; giving the device up is the same exposure a blocking transfer has when it
    // throws on timeout.
    log_error(LogUMD, "DMA transfer on PCI device {} timed out.", pci_device_->get_device_num());
    in_flight_dma_.reset();
    return DmaTransferState::ERROR;
}

std::unique_lock<MutexInterface> PcieProtocol::lock_dma_engine() {
    return LockManager::acquire_mutex(MutexType::PCIE_DMA, pci_device_->get_device_num(), IODeviceType::PCIe);
}

std::pair<std::unique_lock<std::mutex>, std::unique_lock<MutexInterface>>
PcieProtocol::lock_dma_for_blocking_transfer() {
    std::unique_lock lock(dma_mutex_);
    if (in_flight_dma_.has_value() && in_flight_dma_->owner == std::this_thread::get_id()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "This thread has an asynchronous DMA transfer in flight on PCI device {}; check it until it is done "
                "before starting a blocking one.",
                pci_device_->get_device_num()));
    }

    while (in_flight_dma_.has_value()) {
        lock.unlock();
        std::this_thread::yield();
        lock.lock();
    }
    auto pcie_dma_lock = lock_dma_engine();
    return {std::move(lock), std::move(pcie_dma_lock)};
}

TlbWindow* PcieProtocol::get_cached_dma_tlb_window(const tlb_data& config) {
    if (cached_dma_tlb_window_ == nullptr) {
        // The DMA engine, not the host, reads through this window, so it is not ordered behind our
        // config write - pass verify_config so every configure() confirms it landed first.
        auto handle = pci_device_->allocate_tlb(
            get_dma_tlb_size(pci_device_->get_arch()), TlbMapping::WC, /*verify_config=*/true);
        cached_dma_tlb_window_ = std::make_unique<SiliconTlbWindow>(std::move(handle), config);
    }

    return cached_dma_tlb_window_.get();
}

void PcieProtocol::program_dma_engine(
    DmaDirection direction, uint64_t iova_addr, uint32_t tlb_window_addr, size_t size) {
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
    volatile uint8_t* bar2 = reinterpret_cast<volatile uint8_t*>(pci_device_->bar2_uc);

    if (!dma_buffer.completion || !dma_buffer.buffer) {
        UMD_THROW(error::RuntimeError, "DMA buffer is not initialized.");
    }

    if (tlb_window_addr % 4 != 0) {
        UMD_THROW(error::RuntimeError, "DMA device address must be aligned to 4 bytes.");
    }

    if (size % 4 != 0) {
        UMD_THROW(error::RuntimeError, "DMA size must be a multiple of 4.");
    }

    if (!bar2) {
        UMD_THROW(error::RuntimeError, "BAR2 is not mapped.");
    }

    std::visit(
        [&](auto& strategy) {
            if (direction == DmaDirection::H2D) {
                strategy.h2d_start(bar2, dma_buffer, tlb_window_addr, iova_addr, size);
            } else {
                strategy.d2h_start(bar2, dma_buffer, iova_addr, tlb_window_addr, size);
            }
        },
        dma_strategy_);
}

bool PcieProtocol::dma_is_complete(DmaDirection direction) {
    const DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
    return std::visit(
        [&](auto& strategy) {
            return direction == DmaDirection::H2D ? strategy.h2d_is_complete(dma_buffer)
                                                  : strategy.d2h_is_complete(dma_buffer);
        },
        dma_strategy_);
}

void PcieProtocol::run_dma_transfer(DmaDirection direction, uint64_t iova_addr, uint32_t tlb_window_addr, size_t size) {
    program_dma_engine(direction, iova_addr, tlb_window_addr, size);
    const bool completed = utils::poll_until(
        [&]() { return dma_is_complete(direction); },
        timeout::DMA_TIMEOUT,
        timeout::DMA_TIMEOUT,
        std::chrono::microseconds(0));
    UMD_ASSERT(completed, error::RuntimeError, "DMA timeout.");
}

}  // namespace tt::umd
