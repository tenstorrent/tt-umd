/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/pcie_protocol.hpp"

#include <mutex>
#include <stdexcept>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"

namespace tt::umd {

PcieProtocol::PcieProtocol(std::unique_ptr<PCIDevice> pci_device, bool use_safe_api) :
    pci_device_(std::move(pci_device)), use_safe_api_(use_safe_api) {}

PcieProtocol::~PcieProtocol() = default;

TlbWindow* PcieProtocol::get_cached_tlb_window() {
    if (cached_tlb_window_ == nullptr) {
        cached_tlb_window_ = std::make_unique<SiliconTlbWindow>(pci_device_->allocate_tlb(
            pci_device_->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::UC));
    }
    return cached_tlb_window_.get();
}

template <bool safe>
void PcieProtocol::write_to_device_impl(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(io_lock_);
    if constexpr (safe) {
        get_cached_tlb_window()->safe_write_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->write_block_reconfigure(mem_ptr, core, addr, size);
    }
}

template <bool safe>
void PcieProtocol::read_from_device_impl(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(io_lock_);
    if constexpr (safe) {
        get_cached_tlb_window()->safe_read_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->read_block_reconfigure(mem_ptr, core, addr, size);
    }
}

void PcieProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (use_safe_api_) {
        write_to_device_impl<true>(mem_ptr, core, addr, size);
    } else {
        write_to_device_impl<false>(mem_ptr, core, addr, size);
    }
}

void PcieProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (use_safe_api_) {
        read_from_device_impl<true>(mem_ptr, core, addr, size);
    } else {
        read_from_device_impl<false>(mem_ptr, core, addr, size);
    }
}

bool PcieProtocol::write_to_device_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t) { return false; }

void PcieProtocol::noc_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    std::lock_guard<std::mutex> lock(io_lock_);
    get_cached_tlb_window()->noc_multicast_write_reconfigure(src, size, core_start, core_end, addr, tlb_data::Strict);
}

void PcieProtocol::write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void PcieProtocol::bar_write32(uint32_t addr, uint32_t data) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Write Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    *reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(pci_device_->bar0) + addr) = data;
}

uint32_t PcieProtocol::bar_read32(uint32_t addr) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Read Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(pci_device_->bar0) + addr);
}

PCIDevice* PcieProtocol::get_pci_device() { return pci_device_.get(); }

void PcieProtocol::dma_write_to_device(const void*, size_t, tt_xy_pair, uint64_t) {
    throw std::runtime_error("PcieProtocol::dma_write_to_device not yet implemented");
}

void PcieProtocol::dma_read_from_device(void*, size_t, tt_xy_pair, uint64_t) {
    throw std::runtime_error("PcieProtocol::dma_read_from_device not yet implemented");
}

void PcieProtocol::dma_multicast_write(void*, size_t, tt_xy_pair, tt_xy_pair, uint64_t) {
    throw std::runtime_error("PcieProtocol::dma_multicast_write not yet implemented");
}

void PcieProtocol::dma_d2h(void*, uint32_t, size_t) {
    throw std::runtime_error("PcieProtocol::dma_d2h not yet implemented");
}

void PcieProtocol::dma_d2h_zero_copy(void*, uint32_t, size_t) {
    throw std::runtime_error("PcieProtocol::dma_d2h_zero_copy not yet implemented");
}

void PcieProtocol::dma_h2d(uint32_t, const void*, size_t) {
    throw std::runtime_error("PcieProtocol::dma_h2d not yet implemented");
}

void PcieProtocol::dma_h2d_zero_copy(uint32_t, const void*, size_t) {
    throw std::runtime_error("PcieProtocol::dma_h2d_zero_copy not yet implemented");
}

}  // namespace tt::umd
