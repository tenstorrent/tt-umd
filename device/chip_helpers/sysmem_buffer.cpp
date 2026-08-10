// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/sysmem_buffer.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <utility>

#include "tracy.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SysmemBuffer::SysmemBuffer(TTDevice* tt_device, void* buffer_va, size_t buffer_size, bool map_to_noc) :
    pci_device_(tt_device->get_pci_device()),
    tt_device_(tt_device),
    buffer_va_(buffer_va),
    mapped_buffer_size_(buffer_size),
    buffer_size_(buffer_size) {
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "PCI device not available in TTDevice.");
    align_address_and_size();
    if (map_to_noc) {
        std::tie(noc_addr_, device_io_addr_) = pci_device_->map_buffer_to_noc(buffer_va_, mapped_buffer_size_);
    } else {
        device_io_addr_ = pci_device_->map_for_dma(buffer_va_, mapped_buffer_size_);
        noc_addr_ = std::nullopt;
    }
    TracyAllocN(buffer_va_, mapped_buffer_size_, "SysmemBuffer");
}

SysmemBuffer::SysmemBuffer(
    void* buffer_va,
    size_t buffer_size,
    uint64_t device_io_addr,
    std::optional<uint64_t> noc_addr,
    std::function<void()> unmap_callback) :
    pci_device_(nullptr),
    tt_device_(nullptr),
    buffer_va_(buffer_va),
    mapped_buffer_size_(buffer_size),
    buffer_size_(buffer_size),
    device_io_addr_(device_io_addr),
    noc_addr_(noc_addr),
    unmap_callback_(std::move(unmap_callback)) {
    align_address_and_size();
    // Pair with TracyFreeN in the destructor so Tracy sees balanced alloc/free.
    TracyAllocN(buffer_va_, mapped_buffer_size_, "SysmemBuffer");
}

void SysmemBuffer::dma_write_to_device(const size_t offset, size_t size, const tt_xy_pair core, uint64_t addr) {
    ZoneScopedC(tracy::Color::Yellow);

    validate(offset, size);

    // TODO: these are chip functions, figure out how to have these
    // inside sysmem buffer, or we keep API as it is and make application send
    // proper coordinates.
    // core = translate_chip_coord_virtual_to_translated(core);

    tt_device_->dma_write_zero_copy(get_device_io_addr(offset), addr, size, core, get_selected_noc_id());
}

void SysmemBuffer::dma_read_from_device(const size_t offset, size_t size, const tt_xy_pair core, uint64_t addr) {
    ZoneScopedC(tracy::Color::Yellow);

    validate(offset, size);

    // TODO: these are chip functions, figure out how to have these
    // inside sysmem buffer, or we keep API as it is and make application send
    // proper coordinates.
    // core = translate_chip_coord_virtual_to_translated(core);

    tt_device_->dma_read_zero_copy(get_device_io_addr(offset), addr, size, core, get_selected_noc_id());
}

SysmemBuffer::~SysmemBuffer() {
    TracyFreeN(buffer_va_, "SysmemBuffer");
    if (unmap_callback_) {
        unmap_callback_();
        return;
    }
    if (pci_device_ == nullptr) {
        return;
    }
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

void SysmemBuffer::validate(const size_t offset, const size_t size) const {
    if (offset >= buffer_size_ || size > buffer_size_ - offset) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Range starting at {:#x} with size {:#x} is out of bounds for SysmemBuffer of size {:#x}",
                offset,
                size,
                buffer_size_));
    }
}

}  // namespace tt::umd
