// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

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
     * @param tlb_manager Pointer to the TLBManager that manages the TLB entries for this buffer.
     * @param buffer_va Pointer to the virtual address of the buffer in the process address space.
     * @param buffer_size Size of the buffer requested by the user.
     * @param map_to_noc If true, the buffer will be mapped to be accessible over NOC from device.
     */
    SysmemBuffer(TLBManager* tlb_manager, void* buffer_va, size_t buffer_size, bool map_to_noc = false);
    ~SysmemBuffer();

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
     * Does zero copy DMA transfer to the device. Since the buffer is already mapped through KMD, this function
     * will not perform any copying. It will just set up the DMA transfer to the device.
     *
     * @param offset Offset from the start of the buffer. Must be less than the size of the buffer.
     * @param size Size of the data to be transferred. Must be less than or equal to the size of the buffer.
     * @param core Core to which the data will be transferred.
     * @param addr Address on the core to which the data will be transferred.
     */
    void dma_write_to_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr, bool use_noc1);

    /**
     * Does zero copy DMA transfer from the device. Since the buffer is already mapped through KMD, this function
     * will not perform any copying. It will just set up the DMA transfer from the device.
     *
     * @param offset Offset from the start of the buffer. Must be less than the size of the buffer.
     * @param size Size of the data to be transferred. Must be less than or equal to the size of the buffer.
     * @param core Core from which the data will be transferred.
     * @param addr Address on the core from which the data will be transferred.
     */
    void dma_read_from_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr, bool use_noc1);

private:
    /**
     * Aligns the address and size of the buffer to the page size. If the buffer is not aligned to the page size,
     * it will be aligned and the size will be adjusted accordingly. The original buffer size will not be changed.
     * However, behaviour (calculation of offset) of the SysmemBuffer is always going to be based on the original VA and
     * size.
     */
    void align_address_and_size();

    /**
     * Validates that the offset is within the bounds of the buffer.
     * Throws an exception if the offset is out of bounds.
     *
     * @param offset Offset to validate.
     */
    void validate(const size_t offset) const;

    TLBManager* tlb_manager_;

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
};

}  // namespace tt::umd
