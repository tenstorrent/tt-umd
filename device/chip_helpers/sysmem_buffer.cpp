// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/sysmem_buffer.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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

// A unique_ptr with a std::function deleter throws bad_function_call if the function is empty and the
// pointer is non-null, so every buffer gets a callable deleter even when there is nothing to release.
SysmemBuffer::Deleter or_no_op(SysmemBuffer::Deleter deleter) {
    if (deleter) {
        return deleter;
    }
    return [](void*) {};
}

}  // namespace

SysmemBuffer::SysmemBuffer(
    TTDevice* tt_device,
    void* buffer_va,
    size_t buffer_size,
    bool map_to_noc,
    DeviceBufferAccess device_access,
    Deleter release_backing_memory) :
    pci_device_(tt_device->get_pci_device()),
    tt_device_(tt_device),
    buffer_va_(buffer_va),
    mapped_buffer_size_(buffer_size),
    buffer_size_(buffer_size),
    device_access_(device_access),
    communication_id_(tt_device->get_communication_device_id()) {
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "PCI device not available in TTDevice.");
    align_address_and_size();
    if (map_to_noc) {
        std::tie(noc_addr_, device_io_addr_) =
            pci_device_->map_buffer_to_noc(buffer_va_, mapped_buffer_size_, device_access_);
    } else {
        device_io_addr_ = pci_device_->map_for_dma(buffer_va_, mapped_buffer_size_, device_access_);
        noc_addr_ = std::nullopt;
    }
    // Compose the full deleter now that the buffer is aligned and pinned: always unpin, then release the
    // backing memory if this buffer owns it.
    system_memory_ptr_ = std::unique_ptr<void, Deleter>(
        buffer_va_,
        [pci_device = pci_device_,
         mapped_size = mapped_buffer_size_,
         iova = device_io_addr_,
         release = std::move(release_backing_memory)](void* aligned_va) {
            try {
                pci_device->unmap_for_dma(aligned_va, mapped_size);
            } catch (...) {
                log_warning(LogUMD, "Failed to unmap sysmem buffer (size: {:#x}, IOVA: {:#x}).", mapped_size, iova);
            }
            if (release) {
                release(aligned_va);
            }
        });
    TracyAllocN(buffer_va_, mapped_buffer_size_, "SysmemBuffer");
}

SysmemBuffer::SysmemBuffer(
    void* buffer_va,
    size_t buffer_size,
    uint64_t device_io_addr,
    int communication_id,
    std::optional<uint64_t> noc_addr,
    Deleter deleter,
    DeviceBufferAccess device_access) :
    pci_device_(nullptr),
    tt_device_(nullptr),
    buffer_va_(buffer_va),
    mapped_buffer_size_(buffer_size),
    buffer_size_(buffer_size),
    device_io_addr_(device_io_addr),
    noc_addr_(noc_addr),
    device_access_(device_access),
    communication_id_(communication_id) {
    align_address_and_size();
    system_memory_ptr_ = std::unique_ptr<void, Deleter>(buffer_va_, or_no_op(std::move(deleter)));
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
    // Runs the deleter composed at construction: unpin, and free the backing memory if this buffer owns it.
    system_memory_ptr_.reset();
}

void SysmemBuffer::align_address_and_size() {
    static const auto page_size = sysconf(_SC_PAGESIZE);
    uint64_t aligned_buffer_va = reinterpret_cast<uint64_t>(buffer_va_) & ~(page_size - 1);
    offset_from_aligned_addr_ = reinterpret_cast<uint64_t>(buffer_va_) - aligned_buffer_va;
    buffer_va_ = reinterpret_cast<void*>(aligned_buffer_va);
    mapped_buffer_size_ = (mapped_buffer_size_ + offset_from_aligned_addr_ + page_size - 1) & ~(page_size - 1);
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
