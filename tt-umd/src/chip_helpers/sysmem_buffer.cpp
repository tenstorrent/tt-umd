// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_buffer.hpp"

#include <fmt/format.h>

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

#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Wraps a caller-supplied deleter so it is always safe for unique_ptr to run from the destructor.
// Two things are handled here: an empty std::function deleter makes unique_ptr throw bad_function_call
// on a non-null pointer, and an exception escaping the deleter would propagate out of ~SysmemBuffer and
// terminate the process. Cleanup failures are logged instead.
SysmemBuffer::Deleter make_non_throwing(SysmemBuffer::Deleter deleter) {
    return [deleter = std::move(deleter)](void* buffer_va) noexcept {
        if (!deleter) {
            return;
        }
        try {
            deleter(buffer_va);
        } catch (const std::exception& e) {
            log_warning(LogUMD, "Failed to release sysmem buffer backing memory at {}: {}.", buffer_va, e.what());
        } catch (...) {
            log_warning(LogUMD, "Failed to release sysmem buffer backing memory at {}.", buffer_va);
        }
    };
}

}  // namespace

SysmemBuffer::SysmemBuffer(
    void* buffer_va,
    size_t buffer_size,
    uint64_t device_io_addr,
    int communication_id,
    Deleter deleter,
    std::optional<uint64_t> noc_addr,
    DeviceBufferAccess device_access,
    NocBinder noc_binder) :
    buffer_va_(buffer_va),
    buffer_size_(buffer_size),
    device_io_addr_(device_io_addr),
    noc_addr_(noc_addr),
    noc_binder_(std::move(noc_binder)),
    device_access_(device_access),
    communication_id_(communication_id) {
    system_memory_ptr_ = std::unique_ptr<void, Deleter>(buffer_va_, make_non_throwing(std::move(deleter)));
    TracyAllocN(buffer_va_, buffer_size_, "SysmemBuffer");
}

SysmemBuffer::~SysmemBuffer() {
    TracyFreeN(buffer_va_, "SysmemBuffer");
    // Destroying system_memory_ptr_ runs the deleter composed at construction: unpin, and free the
    // backing memory if this buffer owns it.
}

void SysmemBuffer::write_to_sysmem(const void* src, const size_t size, const size_t offset) {
    ZoneScopedC(tracy::Color::Yellow);
    validate(offset, size);
    memcpy(static_cast<uint8_t*>(get_va()) + offset, src, size);
}

void SysmemBuffer::read_from_sysmem(void* dest, const size_t size, const size_t offset) {
    ZoneScopedC(tracy::Color::Yellow);
    validate(offset, size);
    memcpy(dest, static_cast<const uint8_t*>(get_va()) + offset, size);
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

void* SysmemBuffer::get_va() const { return buffer_va_; }

size_t SysmemBuffer::get_size() const { return buffer_size_; }

uint64_t SysmemBuffer::get_iova(const size_t offset) const {
    validate(offset);
    return device_io_addr_ + offset;
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
