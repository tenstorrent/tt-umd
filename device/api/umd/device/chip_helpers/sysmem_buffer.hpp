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
class PCIDevice;
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
     * It encodes the difference between the two ownership models: for a buffer the allocator itself
     * allocated, it unpins the pages and frees the backing memory; for a buffer mapped from a caller's
     * pointer, it only unpins, leaving the memory to its owner.
     */
    using Deleter = std::function<void(void*)>;

    /**
     * Constructor for SysmemBuffer. Start of the buffer must be aligned
     * to page size. In case of unaligned buffer start address, the buffer will be aligned to the page size and the
     * buffer size will be adjusted accordingly. However, the adjusted buffer size won't be visible to the user. It will
     * see a buffer of the original size. Same as for buffer size, user won't be able to access the memory before the
     * start of the buffer, aligning is transparent to the user.
     * Pages separated by | AB - Aligned buffer,
     * UB - Unaligned buffer, UE - Unaligned end, AE - Aligned end
     *
     * |     Page 0     |     Page 1     |     Page 2     |     Page 3     |
     * +----------------+----------------+----------------+----------------+
     * ^                ^       ^                    ^    ^
     * Page Start       AB      UB                   UE   AE
     *                          |<--- buffer_size -->|
     *                  |<----- mapped_buffer_size ----->|
     *
     * @param tt_device Pointer to the TTDevice. Used directly for DMA transfers, and to access the underlying
     * PCIDevice for mapping/unmapping and TLB allocation.
     * @param buffer_va Pointer to the virtual address of the buffer in the process address space.
     * @param buffer_size Size of the buffer requested by the user.
     * @param map_to_noc If true, the buffer will be mapped to be accessible over NOC from device.
     * @param release_backing_memory Optional callable that frees the pages behind buffer_va, invoked after
     * the buffer has been unpinned from the device. Pass it when the allocator owns the memory it is handing
     * over; leave it empty when the caller owns the memory and only wants it unpinned on destruction.
     */
    SysmemBuffer(
        TTDevice* tt_device,
        void* buffer_va,
        size_t buffer_size,
        bool map_to_noc = false,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE,
        Deleter release_backing_memory = {});
    /**
     * Constructor for a buffer that was already made visible to the device by the caller.
     *
     * @param communication_id Identifier of the device this buffer's IOVA is valid for. Supplied by the
     * allocator, which pins for exactly one device. Matches TTDevice::get_communication_device_id().
     * @param deleter Cleanup callable invoked on destruction with the page-aligned start of the buffer.
     * Defaults to releasing nothing, since this constructor takes a mapping the caller already owns.
     */
    SysmemBuffer(
        void* buffer_va,
        size_t buffer_size,
        uint64_t device_io_addr,
        int communication_id,
        std::optional<uint64_t> noc_addr = std::nullopt,
        Deleter deleter = {},
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE);
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
     * Confirms this buffer is bound to a NOC address, so that every tile on the device can reach it rather
     * than only the PCIe tile.
     *
     * The driver assigns the NOC address when the pages are pinned, so binding is decided at construction
     * by the bind_to_noc argument on the allocator, not here. This is therefore a no-op on a bound buffer
     * and throws on an unbound one, which turns a buffer that can never be reached over the NOC into an
     * error at the point of the mistaken assumption rather than at a much later device access.
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
    /**
     * Aligns the address and size of the buffer to the page size. If the buffer is not aligned to the page size,
     * it will be aligned and the size will be adjusted accordingly. The original buffer size will not be changed.
     * However, behaviour (calculation of offset) of the SysmemBuffer is always going to be based on the original VA and
     * size.
     */
    void align_address_and_size();

    /**
     * Validates that the [offset, offset + size) range is within the bounds of the buffer.
     * Throws an exception if the range is out of bounds.
     *
     * @param offset Offset to validate.
     * @param size Size of the range starting at offset to validate. Defaults to 0, meaning only offset is validated.
     */
    void validate(const size_t offset, const size_t size = 0) const;

    PCIDevice* pci_device_;
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

    // Owns the buffer's page-aligned start. Releasing it runs the deleter composed at construction,
    // which unpins the pages from the device and, for buffers this class allocated, frees them.
    std::unique_ptr<void, Deleter> system_memory_ptr_;

    DeviceBufferAccess device_access_ = DeviceBufferAccess::READ_WRITE;

    // Device this buffer's IOVA is valid for. -1 when unknown.
    int communication_id_ = -1;
};

}  // namespace tt::umd
