/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/pcie_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <variant>

#include "noc_access.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"

namespace tt::umd {

DmaTransferStrategy PcieProtocol::create_dma_strategy(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return WormholeDmaTransfer{};
        case tt::ARCH::BLACKHOLE:
            return BlackholeDmaTransfer{};
        default:
            throw std::runtime_error("Unsupported architecture for DMA transfer strategy.");
    }
}

size_t PcieProtocol::get_dma_tlb_size(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            return 2 * 1024 * 1024;
        case tt::ARCH::WORMHOLE_B0:
            return 16 * 1024 * 1024;
        default:
            throw std::runtime_error("Unsupported architecture for DMA TLB size.");
    }
}

PcieProtocol::PcieProtocol(std::unique_ptr<PCIDevice> pci_device, bool use_safe_api) :
    pci_device_(std::move(pci_device)),
    dma_strategy_(create_dma_strategy(pci_device_->get_arch())),
    use_safe_api_(use_safe_api) {}

PcieProtocol::~PcieProtocol() = default;

TlbWindow* PcieProtocol::get_cached_tlb_window() {
    if (cached_tlb_window_ == nullptr) {
        cached_tlb_window_ = std::make_unique<SiliconTlbWindow>(pci_device_->allocate_tlb(
            pci_device_->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::UC));
    }
    return cached_tlb_window_.get();
}

void PcieProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(io_lock_);
    if (use_safe_api_) {
        write_to_device_impl<true>(mem_ptr, core, addr, size);
    } else {
        write_to_device_impl<false>(mem_ptr, core, addr, size);
    }
}

void PcieProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(io_lock_);
    if (use_safe_api_) {
        read_from_device_impl<true>(mem_ptr, core, addr, size);
    } else {
        read_from_device_impl<false>(mem_ptr, core, addr, size);
    }
}

template <bool safe>
void PcieProtocol::write_to_device_impl(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if constexpr (safe) {
        get_cached_tlb_window()->safe_write_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->write_block_reconfigure(mem_ptr, core, addr, size);
    }
}

template <bool safe>
void PcieProtocol::read_from_device_impl(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if constexpr (safe) {
        get_cached_tlb_window()->safe_read_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->read_block_reconfigure(mem_ptr, core, addr, size);
    }
}

bool PcieProtocol::write_to_core_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t) { return false; }

void PcieProtocol::noc_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    std::lock_guard<std::mutex> lock(io_lock_);
    if (use_safe_api_) {
        get_cached_tlb_window()->safe_noc_multicast_write_reconfigure(
            src, size, core_start, core_end, addr, tlb_data::Strict);
    } else {
        get_cached_tlb_window()->noc_multicast_write_reconfigure(
            src, size, core_start, core_end, addr, tlb_data::Strict);
    }
}

void PcieProtocol::write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void PcieProtocol::bar_write32(uint32_t addr, uint32_t data) {
    if (addr < BAR0_OFFSET) {
        throw std::runtime_error("Write Invalid BAR address for this device.");
    }
    addr -= BAR0_OFFSET;
    *reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(pci_device_->bar0) + addr) = data;
}

uint32_t PcieProtocol::bar_read32(uint32_t addr) {
    if (addr < BAR0_OFFSET) {
        throw std::runtime_error("Read Invalid BAR address for this device.");
    }
    addr -= BAR0_OFFSET;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(pci_device_->bar0) + addr);
}

PCIDevice* PcieProtocol::get_pci_device() { return pci_device_.get(); }

bool PcieProtocol::dma_write_to_device(void* src, size_t size, tt_xy_pair core, uint64_t addr) {
    return dma_transfer(src, size, addr, create_dma_tlb_config(addr, core), DmaDirection::H2D);
}

bool PcieProtocol::dma_read_from_device(void* dst, size_t size, tt_xy_pair core, uint64_t addr) {
    return dma_transfer(dst, size, addr, create_dma_tlb_config(addr, core), DmaDirection::D2H);
}

bool PcieProtocol::dma_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    return dma_transfer(src, size, addr, create_dma_tlb_config(addr, core_end, core_start), DmaDirection::H2D);
}

// Creates a TLB config for DMA transfers. Parameters are named core_end/core_start to match
// the x_end/y_end and x_start/y_start fields in tlb_data. For unicast, only core_end is needed
// (the target core). When core_start is provided, the transfer becomes a multicast to the
// core range [core_start, core_end].
tlb_data PcieProtocol::create_dma_tlb_config(uint64_t addr, tt_xy_pair core_end, std::optional<tt_xy_pair> core_start) {
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = pci_device_->get_architecture_implementation()->get_static_vc();
    if (core_start) {
        config.x_start = core_start->x;
        config.y_start = core_start->y;
        config.mcast = true;
    }
    return config;
}

bool PcieProtocol::dma_transfer(void* buffer, size_t size, uint64_t addr, tlb_data config, DmaDirection direction) {
    std::scoped_lock lock(dma_mutex_);
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();

    if (dma_buffer.buffer == nullptr) {
        log_warning(LogUMD, "DMA buffer was not allocated for PCI device {}.", pci_device_->get_device_num());
        return false;
    }

    uint8_t* buf = static_cast<uint8_t*>(buffer);
    size_t dmabuf_size = dma_buffer.size;
    TlbWindow* tlb_window = get_cached_dma_tlb_window(config);

    auto axi_address_base = pci_device_->get_architecture_implementation()
                                ->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id())
                                .tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));

    while (size > 0) {
        auto tlb_size = tlb_window->get_size();
        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        if (direction == DmaDirection::H2D) {
            std::memcpy(dma_buffer.buffer, buf, transfer_size);
            dma_h2d_transfer(static_cast<uint32_t>(axi_address), dma_buffer.buffer_pa, transfer_size);
        } else {
            dma_d2h_transfer(dma_buffer.buffer_pa, static_cast<uint32_t>(axi_address), transfer_size);
            std::memcpy(buf, dma_buffer.buffer, transfer_size);
        }

        size -= transfer_size;
        addr += transfer_size;
        buf += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }

    return true;
}

TlbWindow* PcieProtocol::get_cached_dma_tlb_window(tlb_data config) {
    if (cached_dma_tlb_window_ == nullptr) {
        cached_dma_tlb_window_ = std::make_unique<SiliconTlbWindow>(
            pci_device_->allocate_tlb(get_dma_tlb_size(pci_device_->get_arch()), TlbMapping::WC), config);
        return cached_dma_tlb_window_.get();
    }

    cached_dma_tlb_window_->configure(config);
    return cached_dma_tlb_window_.get();
}

// TODO: These public DMA methods are locked for safety since they can be called directly by
// consumers. The goal is to make the protocol class lockless and push synchronization to
// higher-level components. dma_transfer() calls the private _transfer methods directly to
// avoid lock contention.
void PcieProtocol::dma_d2h(void* dst, uint32_t src, size_t size) {
    std::scoped_lock lock(dma_mutex_);
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    dma_d2h_transfer(dma_buffer.buffer_pa, src, size);
    std::memcpy(dst, dma_buffer.buffer, size);
}

void PcieProtocol::dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) {
    std::scoped_lock lock(dma_mutex_);
    dma_d2h_transfer(reinterpret_cast<uint64_t>(dst), src, size);
}

void PcieProtocol::dma_h2d(uint32_t dst, const void* src, size_t size) {
    std::scoped_lock lock(dma_mutex_);
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();

    if (size > dma_buffer.size) {
        throw std::runtime_error("DMA size exceeds buffer size");
    }

    std::memcpy(dma_buffer.buffer, src, size);
    dma_h2d_transfer(dst, dma_buffer.buffer_pa, size);
}

void PcieProtocol::dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) {
    std::scoped_lock lock(dma_mutex_);
    dma_h2d_transfer(dst, reinterpret_cast<uint64_t>(src), size);
}

void PcieProtocol::dma_d2h_transfer(const uint64_t dst, const uint32_t src, const size_t size) {
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
    volatile uint8_t* bar2 = reinterpret_cast<volatile uint8_t*>(pci_device_->bar2_uc);

    if (!dma_buffer.completion || !dma_buffer.buffer) {
        throw std::runtime_error("DMA buffer is not initialized");
    }

    if (src % 4 != 0) {
        throw std::runtime_error("DMA source address must be aligned to 4 bytes");
    }

    if (size % 4 != 0) {
        throw std::runtime_error("DMA size must be a multiple of 4");
    }

    if (!bar2) {
        throw std::runtime_error("BAR2 is not mapped");
    }

    std::visit([&](auto& strategy) { strategy.d2h_transfer(bar2, dma_buffer, dst, src, size); }, dma_strategy_);
}

void PcieProtocol::dma_h2d_transfer(const uint32_t dst, const uint64_t src, const size_t size) {
    DmaBuffer& dma_buffer = pci_device_->get_dma_buffer();
    volatile uint8_t* bar2 = reinterpret_cast<volatile uint8_t*>(pci_device_->bar2_uc);

    if (!dma_buffer.completion || !dma_buffer.buffer) {
        throw std::runtime_error("DMA buffer is not initialized");
    }

    if (dst % 4 != 0) {
        throw std::runtime_error("DMA destination address must be aligned to 4 bytes");
    }

    if (size % 4 != 0) {
        throw std::runtime_error("DMA size must be a multiple of 4");
    }

    if (!bar2) {
        throw std::runtime_error("BAR2 is not mapped");
    }

    std::visit([&](auto& strategy) { strategy.h2d_transfer(bar2, dma_buffer, dst, src, size); }, dma_strategy_);
}

}  // namespace tt::umd
