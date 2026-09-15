// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/sysmem_buffer.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
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

namespace {

// Wraps a caller-supplied deleter so it is always safe for unique_ptr to run from the destructor.
// Two things are handled here: an empty std::function deleter makes unique_ptr throw bad_function_call
// on a non-null pointer, and an exception escaping the deleter would propagate out of ~SysmemBuffer and
// terminate the process. Cleanup failures are logged instead.
SysmemBuffer::Deleter make_non_throwing(SysmemBuffer::Deleter deleter) {
    return [deleter = std::move(deleter)](void* aligned_va) noexcept {
        if (!deleter) {
            return;
        }
        try {
            deleter(aligned_va);
        } catch (const std::exception& e) {
            log_warning(LogUMD, "Failed to release sysmem buffer backing memory at {}: {}.", aligned_va, e.what());
        } catch (...) {
            log_warning(LogUMD, "Failed to release sysmem buffer backing memory at {}.", aligned_va);
        }
    };
}

}  // namespace

SysmemBuffer::AlignedRange SysmemBuffer::page_align(void* buffer_va, size_t buffer_size) {
    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t va = reinterpret_cast<uint64_t>(buffer_va);
    const uint64_t base = va & ~(page_size - 1);
    AlignedRange range{};
    range.base = reinterpret_cast<void*>(base);
    range.offset_from_base = va - base;
    range.mapped_size = (buffer_size + range.offset_from_base + page_size - 1) & ~(page_size - 1);
    return range;
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
    buffer_size_(buffer_size),
    device_io_addr_(device_io_addr),
    noc_addr_(noc_addr),
    noc_binder_(std::move(noc_binder)),
    device_access_(device_access),
    communication_id_(communication_id) {
    const AlignedRange range = page_align(buffer_va, buffer_size);
    buffer_va_ = range.base;
    mapped_buffer_size_ = range.mapped_size;
    offset_from_aligned_addr_ = range.offset_from_base;

    system_memory_ptr_ = std::unique_ptr<void, Deleter>(buffer_va_, make_non_throwing(std::move(deleter)));
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

    if (device_access_ == DeviceBufferAccess::READ_ONLY) {
        UMD_THROW(error::RuntimeError, "Cannot DMA from the device into a device-read-only host mapping.");
    }

    validate(offset, size);

    // TODO: these are chip functions, figure out how to have these
    // inside sysmem buffer, or we keep API as it is and make application send
    // proper coordinates.
    // core = translate_chip_coord_virtual_to_translated(core);

    tt_device_->dma_read_zero_copy(get_device_io_addr(offset), addr, size, core, get_selected_noc_id());
}

SysmemBuffer::~SysmemBuffer() {
    TracyFreeN(buffer_va_, "SysmemBuffer");
    // Destroying system_memory_ptr_ runs the deleter composed at construction: unpin, and free the
    // backing memory if this buffer owns it.
}

void SysmemBuffer::write_to_sysmem(const void* src, const size_t size, const size_t offset) {
    ZoneScopedC(tracy::Color::Yellow);
    validate(offset, size);
    memcpy(static_cast<uint8_t*>(get_buffer_va()) + offset, src, size);
}

void SysmemBuffer::read_from_sysmem(void* dest, const size_t size, const size_t offset) {
    ZoneScopedC(tracy::Color::Yellow);
    validate(offset, size);
    memcpy(dest, static_cast<const uint8_t*>(get_buffer_va()) + offset, size);
}

void SysmemBuffer::bind_noc_address() {
    if (noc_addr_.has_value()) {
        return;
    }
    if (!noc_binder_) {
        UMD_THROW(
            error::RuntimeError,
            "This sysmem buffer has no NOC address and no way to bind one. Its allocator binds at pin time, so "
            "allocate or map the buffer with bind_to_noc set instead.");
    }
    noc_addr_ = noc_binder_();
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
