/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/sysmem_buffer.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
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

    static const std::string tlb_name = "LARGE_WRITE_TLB";

    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(get_device_io_addr(offset));

    auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(tlb_name);
    auto ordering = tlb_manager_->dynamic_tlb_ordering_modes_.at(tlb_name);
    PCIDevice* pci_device = tt_device_->get_pci_device().get();

    // TODO: these are chip functions, figure out how to have these
    // inside sysmem buffer, or we keep API as it is and make application send
    // proper coordinates.
    // core = translate_chip_coord_virtual_to_translated(core);
    // auto lock = acquire_mutex(tlb_name, pci_device->get_device_num());
    while (size > 0) {
        auto [axi_address, tlb_size] = tt_device_->set_dynamic_tlb(tlb_index, core, addr, ordering);

        size_t transfer_size = std::min({size, tlb_size});

        tt_device_->dma_h2d_zero_copy(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;
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

    static const std::string tlb_name = "LARGE_READ_TLB";
    uint8_t* buffer = reinterpret_cast<uint8_t*>(get_device_io_addr(offset));
    auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(tlb_name);
    auto ordering = tlb_manager_->dynamic_tlb_ordering_modes_.at(tlb_name);
    PCIDevice* pci_device = tt_device_->get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    // TODO: these are chip functions, figure out how to have these
    // inside sysmem buffer, or we keep API as it is and make application send
    // proper coordinates.
    // core = translate_chip_coord_virtual_to_translated(core);
    // auto lock = acquire_mutex(tlb_name, pci_device->get_device_num());

    while (size > 0) {
        auto [axi_address, tlb_size] = tt_device_->set_dynamic_tlb(tlb_index, core, addr, ordering);

        size_t transfer_size = std::min({size, tlb_size});

        tt_device_->dma_d2h_zero_copy(buffer, axi_address, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;
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
    uint64_t unaligned_buffer_va = reinterpret_cast<uint64_t>(buffer_va_);
    uint64_t aligned_buffer_va = reinterpret_cast<uint64_t>(buffer_va_) & ~(page_size - 1);
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

}  // namespace tt::umd
