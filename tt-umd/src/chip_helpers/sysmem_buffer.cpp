// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_buffer.hpp"

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "common/unsupported.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Runs on destruction, so a failing deleter must not escape: a throwing destructor terminates the process.
SysmemBuffer::Deleter make_noexcept_deleter(SysmemBuffer::Deleter deleter) {
    return [deleter = std::move(deleter)](void* aligned_base) noexcept {
        if (!deleter) {
            return;
        }
        try {
            deleter(aligned_base);
        } catch (const std::exception& e) {
            log_error(LogUMD, "Releasing sysmem buffer at {} failed: {}", aligned_base, e.what());
        } catch (...) {
            log_error(LogUMD, "Releasing sysmem buffer at {} failed with an unknown exception.", aligned_base);
        }
    };
}

}  // namespace

SysmemBuffer::AlignedRange SysmemBuffer::page_align(void* buffer_va, size_t buffer_size) {
    const uint64_t page_size = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    const uint64_t va = reinterpret_cast<uintptr_t>(buffer_va);
    const uint64_t base = va & ~(page_size - 1);
    const uint64_t offset_from_base = va - base;

    // The rounded-up end must stay representable, otherwise the extent wraps and covers too little.
    const uint64_t max_unaligned_size = std::numeric_limits<uint64_t>::max() - (page_size - 1) - offset_from_base;
    UMD_ASSERT(
        buffer_size <= max_unaligned_size,
        error::RuntimeError,
        "Sysmem buffer of size " + std::to_string(buffer_size) + " does not fit in the address space.");

    const uint64_t mapped_size = (offset_from_base + buffer_size + page_size - 1) & ~(page_size - 1);
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
    buffer_size_(buffer_size),
    device_io_addr_(device_io_addr),
    noc_addr_(noc_addr),
    noc_binder_(std::move(noc_binder)),
    system_memory_ptr_(nullptr, make_noexcept_deleter(std::move(deleter))),
    device_access_(device_access),
    communication_id_(communication_id) {
    const AlignedRange aligned = page_align(buffer_va, buffer_size);
    mapped_buffer_size_ = aligned.mapped_size;
    offset_from_aligned_addr_ = aligned.offset_from_base;
    system_memory_ptr_.reset(aligned.base);
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
        noc_binder_,
        error::RuntimeError,
        "Sysmem buffer was not pinned with NOC access and its allocator cannot bind it afterwards.");
    noc_addr_ = noc_binder_();
}

void SysmemBuffer::dma_write_to_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr) {
    UMD_NOT_IMPLEMENTED();
}

void SysmemBuffer::dma_read_from_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr) {
    UMD_NOT_IMPLEMENTED();
}

void SysmemBuffer::validate(const size_t offset, const size_t size) const {
    // Written so that neither side can overflow: offset is checked first, then size against what is left.
    UMD_ASSERT(
        offset < buffer_size_ && size <= buffer_size_ - offset,
        error::RuntimeError,
        "Range [" + std::to_string(offset) + ", " + std::to_string(offset) + " + " + std::to_string(size) +
            ") is out of bounds of the sysmem buffer of size " + std::to_string(buffer_size_) + ".");
}

}  // namespace tt::umd
