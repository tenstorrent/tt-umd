// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/sysmem_buffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tt-logger/tt-logger.hpp>
#include <tuple>

#include "assert.hpp"
#include "noc_access.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

SysmemBuffer::SysmemBuffer(TLBManager* tlb_manager, void* buffer_va, size_t buffer_size, bool map_to_noc) :
    tlb_manager_(tlb_manager), buffer_va_(buffer_va), mapped_buffer_size_(buffer_size), buffer_size_(buffer_size) {
    align_address_and_size();
    PCIDevice* pci_device = tlb_manager->get_tt_device()->get_pci_device().get();
    if (map_to_noc) {
        std::tie(noc_addr_, device_io_addr_) = pci_device->map_buffer_to_noc(buffer_va_, mapped_buffer_size_);
    } else {
        device_io_addr_ = pci_device->map_for_dma(buffer_va_, mapped_buffer_size_);
        noc_addr_ = std::nullopt;
    }
}

void SysmemBuffer::dma_write_to_device(const size_t offset, size_t size, const tt_xy_pair core, uint64_t addr) {
    TTDevice* tt_device_ = tlb_manager_->get_tt_device();

    if (tt_device_->get_pci_device()->get_dma_buffer().buffer == nullptr) {
        TT_THROW(
            "DMA buffer is not allocated on PCI device {}, PCIe DMA operations not supported.",
            tt_device_->get_pci_device()->get_device_num());
    }

    validate(offset);

    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(get_device_io_addr(offset));

    // TODO: these are chip functions, figure out how to have these
    // inside sysmem buffer, or we keep API as it is and make application send
    // proper coordinates.
    // core = translate_chip_coord_virtual_to_translated(core);

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = tlb_manager_->get_tt_device()->get_architecture_implementation()->get_static_vc();
    TlbWindow* tlb_window = get_cached_tlb_window();
    tlb_window->configure(config);

    auto axi_address_base = tt_device_->get_architecture_implementation()
                                ->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id())
                                .tlb_offset;
    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();

    // In order to properly initiate DMA transfer, we need to calculate the offset into the TLB window
    // based on the target address. Bitwise operations work in this case since all our TLB windows are power-of-two
    // sized.
    auto axi_address = axi_address_base + (addr & (tlb_handle_size - 1));

    while (size > 0) {
        auto tlb_size = tlb_window->get_size();

        size_t const transfer_size = std::min({size, tlb_size});

        tt_device_->dma_h2d_zero_copy(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void SysmemBuffer::dma_read_from_device(const size_t offset, size_t size, const tt_xy_pair core, uint64_t addr) {
    TTDevice* tt_device_ = tlb_manager_->get_tt_device();

    if (tt_device_->get_pci_device()->get_dma_buffer().buffer == nullptr) {
        TT_THROW(
            "DMA buffer is not allocated on PCI device {}, PCIe DMA operations not supported.",
            tt_device_->get_pci_device()->get_device_num());
    }

    validate(offset);
    uint8_t* buffer = reinterpret_cast<uint8_t*>(get_device_io_addr(offset));

    // TODO: these are chip functions, figure out how to have these
    // inside sysmem buffer, or we keep API as it is and make application send
    // proper coordinates.
    // core = translate_chip_coord_virtual_to_translated(core);

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = tlb_manager_->get_tt_device()->get_architecture_implementation()->get_static_vc();

    TlbWindow* tlb_window = get_cached_tlb_window();
    tlb_window->configure(config);

    auto axi_address_base = tt_device_->get_architecture_implementation()
                                ->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id())
                                .tlb_offset;
    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();

    // In order to properly initiate DMA transfer, we need to calculate the offset into the TLB window
    // based on the target address. Bitwise operations work in this case since all our TLB windows are power-of-two
    // sized.
    auto axi_address = axi_address_base + (addr & (tlb_handle_size - 1));

    while (size > 0) {
        auto tlb_size = tlb_window->get_size();
        size_t const transfer_size = std::min({size, tlb_size});

        tt_device_->dma_d2h_zero_copy(buffer, axi_address, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

SysmemBuffer::~SysmemBuffer() {
    try {
        tlb_manager_->get_tt_device()->get_pci_device()->unmap_for_dma(buffer_va_, mapped_buffer_size_);
    } catch (...) {
        log_warning(
            LogUMD, "Failed to unmap sysmem buffer (size: {:#x}, IOVA: {:#x}).", mapped_buffer_size_, device_io_addr_);
    }
}

void SysmemBuffer::align_address_and_size() {
    static const auto page_size = sysconf(_SC_PAGESIZE);
    uint64_t const aligned_buffer_va = reinterpret_cast<uint64_t>(buffer_va_) & ~(page_size - 1);
    offset_from_aligned_addr_ = reinterpret_cast<uint64_t>(buffer_va_) - aligned_buffer_va;
    buffer_va_ = reinterpret_cast<void*>(aligned_buffer_va);
    mapped_buffer_size_ = (mapped_buffer_size_ + offset_from_aligned_addr_ + page_size - 1) & ~(page_size - 1);
}

void* SysmemBuffer::get_buffer_va() const { return static_cast<uint8_t*>(buffer_va_) + offset_from_aligned_addr_; }

size_t SysmemBuffer::get_buffer_size() const { return buffer_size_; }

uint64_t SysmemBuffer::get_device_io_addr(const size_t offset) const {
    validate(offset);
    return device_io_addr_ + offset + offset_from_aligned_addr_;
}

void SysmemBuffer::validate(const size_t offset) const {
    if (offset >= buffer_size_) {
        TT_THROW("Offset {:#x} is out of bounds for SysmemBuffer of size {#:x}", offset, buffer_size_);
    }
}

TlbWindow* SysmemBuffer::get_cached_tlb_window() {
    if (cached_tlb_window == nullptr) {
        cached_tlb_window = std::make_unique<TlbWindow>(tlb_manager_->get_tt_device()->get_pci_device()->allocate_tlb(
            tlb_manager_->get_tt_device()->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::WC));
        return cached_tlb_window.get();
    }
    return cached_tlb_window.get();
}

}  // namespace tt::umd
