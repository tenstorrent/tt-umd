// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/sysmem_buffer.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tt-logger/tt-logger.hpp>
#include <tuple>

#include "tracy.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SysmemBuffer::SysmemBuffer(PCIDevice* pci_device, void* buffer_va, size_t buffer_size, bool map_to_noc) :
    pci_device_(pci_device), buffer_va_(buffer_va), mapped_buffer_size_(buffer_size), buffer_size_(buffer_size) {
    align_address_and_size();
    if (map_to_noc) {
        std::tie(noc_addr_, device_io_addr_) = pci_device_->map_buffer_to_noc(buffer_va_, mapped_buffer_size_);
    } else {
        device_io_addr_ = pci_device_->map_for_dma(buffer_va_, mapped_buffer_size_);
        noc_addr_ = std::nullopt;
    }
    TracyAllocN(buffer_va_, mapped_buffer_size_, "SysmemBuffer");
}

SysmemBuffer::~SysmemBuffer() {
    TracyFreeN(buffer_va_, "SysmemBuffer");
    try {
        pci_device_->unmap_for_dma(buffer_va_, mapped_buffer_size_);
    } catch (...) {
        log_warning(
            LogUMD, "Failed to unmap sysmem buffer (size: {:#x}, IOVA: {:#x}).", mapped_buffer_size_, device_io_addr_);
    }
}

void SysmemBuffer::align_address_and_size() {
    static const auto page_size = sysconf(_SC_PAGESIZE);
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
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Offset {:#x} is out of bounds for SysmemBuffer of size {:#x}", offset, buffer_size_));
    }
}

TlbWindow* SysmemBuffer::get_cached_tlb_window() {
    if (cached_tlb_window == nullptr) {
        cached_tlb_window = std::make_unique<SiliconTlbWindow>(pci_device_->allocate_tlb(
            pci_device_->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::WC));
    }
    return cached_tlb_window.get();
}

}  // namespace tt::umd
