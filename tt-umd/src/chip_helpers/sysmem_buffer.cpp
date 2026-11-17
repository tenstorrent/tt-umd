// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "common/unsupported.hpp"

namespace tt::umd {

SysmemBuffer::AlignedRange SysmemBuffer::page_align(void* buffer_va, size_t buffer_size) { UMD_NOT_IMPLEMENTED(); }

SysmemBuffer::SysmemBuffer(
    TTDevice* tt_device,
    void* buffer_va,
    size_t buffer_size,
    uint64_t device_io_addr,
    int communication_id,
    Deleter deleter,
    std::optional<uint64_t> noc_addr,
    DeviceBufferAccess device_access,
    NocBinder noc_binder) {
    UMD_NOT_IMPLEMENTED();
}

SysmemBuffer::~SysmemBuffer() = default;

void SysmemBuffer::write_to_sysmem(const void* src, size_t size, size_t offset) { UMD_NOT_IMPLEMENTED(); }

void SysmemBuffer::read_from_sysmem(void* dest, size_t size, size_t offset) { UMD_NOT_IMPLEMENTED(); }

void* SysmemBuffer::get_va() const { UMD_NOT_IMPLEMENTED(); }

size_t SysmemBuffer::get_size() const { UMD_NOT_IMPLEMENTED(); }

uint64_t SysmemBuffer::get_iova(const size_t offset) const { UMD_NOT_IMPLEMENTED(); }

void SysmemBuffer::bind_noc_address() { UMD_NOT_IMPLEMENTED(); }

void SysmemBuffer::dma_write_to_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr) {
    UMD_NOT_IMPLEMENTED();
}

void SysmemBuffer::dma_read_from_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr) {
    UMD_NOT_IMPLEMENTED();
}

void SysmemBuffer::validate(const size_t offset, const size_t size) const { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
