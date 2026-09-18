// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "umd/device/types/host_memory.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class TTDevice;

/**
 * SysmemBuffer class should represent the resource of the HOST memory that is visible to the device.
 * Currently, there are two types of sysmem buffers:
 * 1. Hugepage-based sysmem buffer, that represents old system memory scheme used, that we still want to support until
 * transition to IOMMU is complete.
 * 2. Sysmem buffer, that is used when the system is protected by an IOMMU. With IOMMU, the mappings can be requested at
 * much finer granularity than hugepages.
 *
 * Traditionally, we have referred to the sysmem buffer as something that is
 * visible to device, has its own NOC address. Without changes to KMD, this is still not fully supported for IOMMU
 * buffers.
 */
class SysmemBuffer {
public:
    /**
     * Cleanup callable invoked on destruction, receiving the page-aligned start of the buffer.
     *
     * The allocator is its sole author and states the ownership model outright: unpin-and-free for
     * memory UMD owns, unpin-only for memory the client owns.
     */
    using Deleter = std::function<void(void*)>;

    /**
     * A buffer's page-aligned extent. The pages an allocator pins must cover the whole user range, so an
     * unaligned start is rounded down and the size grown to match.
     */
    struct AlignedRange {
        void* base = nullptr;           // Page-aligned start. This is what gets pinned and released.
        size_t mapped_size = 0;         // Page-rounded size covering the user's whole range.
        uint64_t offset_from_base = 0;  // Distance from base to the user's virtual address.
    };

    /**
     * Computes the page-aligned extent covering [buffer_va, buffer_va + buffer_size).
     *
     * Allocators use this to pin exactly the range the buffer will report offsets against. It is a pure
     * function of its arguments, so the allocator and the buffer independently agree on the extent.
     */
    static AlignedRange page_align(void* buffer_va, size_t buffer_size);

    /**
     * Callable that programs the hardware address translation binding this buffer to the NOC, returning
     * the resulting NOC address.
     *
     * Injected by the allocator, which holds the device context the translation needs. Optional: an
     * allocator whose driver assigns the NOC address at pin time supplies none, and a buffer without one
     * cannot be bound after construction.
     */
    using NocBinder = std::function<uint64_t()>;

    ~SysmemBuffer();

    /**
     * Copies data from a caller-provided source buffer into this system memory buffer.
     * Pure host-side operation, the device is not involved. This is allowed regardless of
     * DeviceBufferAccess, which only constrains what the device may do to the mapping.
     *
     * @param src Pointer to the source host memory.
     * @param size Number of bytes to copy.
     * @param offset Byte offset within this buffer to write to. offset + size must fit in the buffer.
     */
    void write_to_sysmem(const void* src, size_t size, size_t offset);

    /**
     * Copies data from this system memory buffer into a caller-provided destination buffer.
     * Pure host-side operation, the device is not involved.
     *
     * @param dest Pointer to the destination host memory.
     * @param size Number of bytes to copy.
     * @param offset Byte offset within this buffer to read from. offset + size must fit in the buffer.
     */
    void read_from_sysmem(void* dest, size_t size, size_t offset);

    /**
     * Returns the virtual address of the buffer in the process address space.
     * Both in case of aligned and unaligned buffers, this will return the original buffer address.
     */
    void* get_buffer_va() const;

    /**
     * Returns the size of the buffer passed by the user.
     *
     * @return Size of the buffer passed by the user.
     */
    size_t get_buffer_size() const;

    /**
     * Returns device IOVA (IO virtual address) of the buffer on the offset from the start of the buffer.
     *
     * @param offset Offset from the start of the buffer. Must be less than the size of the buffer.
     * @return Device IOVA of the buffer on the offset from the start of the buffer.
     */
    uint64_t get_device_io_addr(const size_t offset = 0) const;

    std::optional<uint64_t> get_noc_addr() const { return noc_addr_; }

    /**
     * Binds a NOC address to this buffer, so every tile on the device can reach it rather than only the
     * PCIe tile.
     *
     * Idempotent: a no-op once bound, whether that happened here or at pin time. Throws when the buffer is
     * unbound and has no binder, which is the case for a buffer whose pages were not pinned with NOC access
     * on a driver that only assigns NOC addresses at pin time.
     */
    void bind_noc_address();

    DeviceBufferAccess get_device_access() const { return device_access_; }

    /**
     * Returns the identifier of the device context this buffer was pinned for. The IOVA is valid only
     * for that device, so callers can compare this against TTDevice::get_communication_device_id() to
     * confirm a buffer belongs to the device it is about to be used with.
     */
    int get_communication_id() const { return communication_id_; }

    /**
     * Does zero copy DMA transfer to the device. Since the buffer is already mapped through KMD, this function
     * will not perform any copying. It will just set up the DMA transfer to the device.
     *
     * @param offset Offset from the start of the buffer. Must be less than the size of the buffer.
     * @param size Size of the data to be transferred. Must be less than or equal to the size of the buffer.
     * @param core Core to which the data will be transferred.
     * @param addr Address on the core to which the data will be transferred.
     */
    void dma_write_to_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr);

    /**
     * Does zero copy DMA transfer from the device. Since the buffer is already mapped through KMD, this function
     * will not perform any copying. It will just set up the DMA transfer from the device.
     *
     * @param offset Offset from the start of the buffer. Must be less than the size of the buffer.
     * @param size Size of the data to be transferred. Must be less than or equal to the size of the buffer.
     * @param core Core from which the data will be transferred.
     * @param addr Address on the core from which the data will be transferred.
     */
    void dma_read_from_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr);

private:
    // Buffers are created only by an allocator, which pins the pages and therefore knows the IOVA, the NOC
    // address and how the memory has to be released.
    friend class SystemMemoryAllocator;

    /**
     * Constructs a buffer over host memory the allocator has already pinned for the device.
     *
     * Alignment stays invisible to the user: get_buffer_va() and get_buffer_size() report what was
     * passed in, and offsets are bounded by that size. The allocator must pin the same aligned range,
     * which it computes with page_align().
     *
     * @param tt_device Device this buffer belongs to, used for the zero-copy DMA helpers. May be null for
     * buffers never used for DMA, such as the simulator's.
     * @param buffer_va Virtual address of the buffer as the user sees it.
     * @param buffer_size Size of the buffer requested by the user.
     * @param device_io_addr IOVA of the page-aligned start, as returned by the allocator's pinning call.
     * @param communication_id Identifier of the device this buffer's IOVA is valid for.
     * @param deleter Cleanup callable invoked on destruction with the page-aligned start.
     * @param noc_addr NOC address, if the pages were pinned with NOC access.
     * @param device_access Whether the device may write the mapping or only read it.
     * @param noc_binder Optional callable for deferred NOC binding.
     */
    SysmemBuffer(
        TTDevice* tt_device,
        void* buffer_va,
        size_t buffer_size,
        uint64_t device_io_addr,
        int communication_id,
        Deleter deleter,
        std::optional<uint64_t> noc_addr = std::nullopt,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE,
        NocBinder noc_binder = {});

    /**
     * Validates that the [offset, offset + size) range is within the bounds of the buffer.
     * Throws an exception if the range is out of bounds.
     *
     * @param offset Offset to validate.
     * @param size Size of the range starting at offset to validate. Defaults to 0, meaning only offset is validated.
     */
    void validate(const size_t offset, const size_t size = 0) const;

    TTDevice* tt_device_;

    // Virtual address in process addr space.
    void* buffer_va_;

    // Size of the memory that is mapped through KMD to be visible to the device.
    size_t mapped_buffer_size_;

    // Size of the buffer requested by user. If the buffer is not aligned to the page size, size of the memory
    // mapped through KMD will be larger than this. This is used to return the size of the buffer requested by the user.
    // Offsets in other SysmemBuffer functions are not allowed to be larger than this size.
    size_t buffer_size_;

    // Address that is used on the system bus to access the beginning of the mapped buffer.
    uint64_t device_io_addr_;

    uint64_t offset_from_aligned_addr_ = 0;

    // Address that is used on the NOC to access the buffer.  NOC target must be
    // the PCIE core that is connected to the host and this address.
    std::optional<uint64_t> noc_addr_;

    // Programs the NOC translation on demand. Empty when the buffer cannot be bound after construction.
    NocBinder noc_binder_;

    // Owns the buffer's page-aligned start. Releasing it runs the deleter composed at construction,
    // which unpins the pages from the device and, for buffers this class allocated, frees them.
    std::unique_ptr<void, Deleter> system_memory_ptr_;

    DeviceBufferAccess device_access_ = DeviceBufferAccess::READ_WRITE;

    // Device this buffer's IOVA is valid for. -1 when unknown.
    int communication_id_ = -1;
};

}  // namespace tt::umd
