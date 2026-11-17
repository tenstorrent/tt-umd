// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_buffer.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <cstring>
#include <exception>
#include <limits>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// The deleter runs from the destructor, so it must tolerate being empty and must never let an exception escape.
SysmemBuffer::Deleter make_safe_deleter(SysmemBuffer::Deleter deleter) {
    return [deleter = std::move(deleter)](void* aligned_start) {
        if (!deleter) {
            return;
        }
        try {
            deleter(aligned_start);
        } catch (const std::exception& e) {
            log_warning(tt::LogUMD, "Releasing a system memory buffer failed: {}", e.what());
        }
    };
}

}  // namespace

SysmemBuffer::AlignedRange SysmemBuffer::page_align(void* buffer_va, size_t buffer_size) {
    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const uintptr_t va = reinterpret_cast<uintptr_t>(buffer_va);
    const uintptr_t base = va & ~(static_cast<uintptr_t>(page_size) - 1);
    const uint64_t offset_from_base = va - base;

    UMD_ASSERT(
        buffer_size <= std::numeric_limits<size_t>::max() - offset_from_base - (page_size - 1),
        error::RuntimeError,
        "Buffer size overflows the address space when rounded up to whole pages.");
    const size_t unaligned_extent = offset_from_base + buffer_size;
    const size_t mapped_size = (unaligned_extent + page_size - 1) & ~(page_size - 1);

    return AlignedRange{reinterpret_cast<void*>(base), mapped_size, offset_from_base};
}

SysmemBuffer::SysmemBuffer(
    TTDevice* tt_device,
    void* buffer_va,
    size_t buffer_size,
    uint64_t device_io_addr,
    int communication_id,
    Deleter deleter,
    std::optional<uint64_t> noc_addr,
    DeviceBufferAccess device_access,
    NocBinder noc_binder) :
    tt_device_(tt_device),
    buffer_va_(buffer_va),
    mapped_buffer_size_(0),
    buffer_size_(buffer_size),
    device_io_addr_(device_io_addr),
    noc_addr_(noc_addr),
    noc_binder_(std::move(noc_binder)),
    system_memory_ptr_(nullptr, make_safe_deleter(std::move(deleter))),
    device_access_(device_access),
    communication_id_(communication_id) {
    const AlignedRange range = page_align(buffer_va, buffer_size);
    mapped_buffer_size_ = range.mapped_size;
    offset_from_aligned_addr_ = range.offset_from_base;
    system_memory_ptr_.reset(range.base);
}

SysmemBuffer::~SysmemBuffer() = default;

void SysmemBuffer::write_to_sysmem(const void* src, size_t size, size_t offset) {
    validate(offset, size);
    std::memcpy(static_cast<uint8_t*>(buffer_va_) + offset, src, size);
}

void SysmemBuffer::read_from_sysmem(void* dest, size_t size, size_t offset) {
    validate(offset, size);
    std::memcpy(dest, static_cast<const uint8_t*>(buffer_va_) + offset, size);
}

void* SysmemBuffer::get_va() const { return buffer_va_; }

size_t SysmemBuffer::get_size() const { return buffer_size_; }

uint64_t SysmemBuffer::get_iova(const size_t offset) const {
    validate(offset);
    return device_io_addr_ + offset_from_aligned_addr_ + offset;
}

void SysmemBuffer::bind_noc_address() {
    if (noc_addr_.has_value()) {
        return;
    }
    UMD_ASSERT(
        static_cast<bool>(noc_binder_),
        error::RuntimeError,
        "Buffer was not pinned with NOC access and its allocator cannot bind a NOC address afterwards.");
    noc_addr_ = noc_binder_();
}

void SysmemBuffer::dma_write_to_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr) {
    validate(offset, size);
    UMD_ASSERT(tt_device_ != nullptr, error::RuntimeError, "Buffer is not associated with a device to DMA to.");
    // The core is already in the coordinates the NOC routes by, so it is passed through untranslated.
    tt_device_->dma_write_zero_copy(
        get_iova(offset), addr, size, CoreCoord(core, CoreType::UNSPECIFIED, CoordSystem::LITERAL));
}

void SysmemBuffer::dma_read_from_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr) {
    UMD_ASSERT(
        device_access_ != DeviceBufferAccess::READ_ONLY,
        error::RuntimeError,
        "Device-to-host DMA would write a buffer the device may only read.");
    validate(offset, size);
    UMD_ASSERT(tt_device_ != nullptr, error::RuntimeError, "Buffer is not associated with a device to DMA from.");
    tt_device_->dma_read_zero_copy(
        get_iova(offset), addr, size, CoreCoord(core, CoreType::UNSPECIFIED, CoordSystem::LITERAL));
}

void SysmemBuffer::validate(const size_t offset, const size_t size) const {
    UMD_ASSERT(
        offset < buffer_size_ && size <= buffer_size_ - offset,
        error::RuntimeError,
        fmt::format(
            "Range [{}, {}) is outside the {}-byte system memory buffer.", offset, offset + size, buffer_size_));
}

}  // namespace tt::umd
