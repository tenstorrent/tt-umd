// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

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
 *
 * The class is platform-agnostic: the owning SysmemManager is responsible for performing any
 * platform-specific mapping (e.g. KMD pin-and-map on silicon, paddr-region registration on the
 * TTSim simulator) and passing the resulting addresses into the constructor. Cleanup work that
 * mirrors the mapping is supplied as the on_destroy callback.
 */
class SysmemBuffer {
public:
    /**
     * @param tlb_manager TLB manager used by the silicon DMA fast paths (dma_write_to_device /
     *                    dma_read_from_device). May be nullptr when those paths are not used
     *                    (e.g. simulation).
     * @param buffer_va   Host virtual address of the buffer as the user sees it. Returned verbatim
     *                    by get_buffer_va().
     * @param buffer_size Size of the user-visible buffer.
     * @param device_io_addr Device IO (system bus) address corresponding to user offset 0. The
     *                       owning manager has already accounted for any internal alignment
     *                       padding; get_device_io_addr(offset) simply returns this + offset.
     * @param noc_addr    NOC address corresponding to user offset 0, when the buffer was mapped
     *                    visible on the NOC. std::nullopt otherwise.
     * @param on_destroy  Optional cleanup callback invoked from the destructor. The owning manager
     *                    captures whatever state is needed to undo its mapping (KMD unmap on
     *                    silicon, region deregistration / munmap on simulation).
     */
    SysmemBuffer(
        TLBManager* tlb_manager,
        void* buffer_va,
        size_t buffer_size,
        uint64_t device_io_addr,
        std::optional<uint64_t> noc_addr,
        std::function<void()> on_destroy = {});
    ~SysmemBuffer();

    /**
     * Returns the virtual address of the buffer in the process address space (the same pointer the
     * user passed in to the manager).
     */
    void* get_buffer_va() const;

    /**
     * Returns the size of the buffer passed by the user.
     */
    size_t get_buffer_size() const;

    /**
     * Returns device IOVA (IO virtual address) of the buffer at the offset from the start of the
     * buffer.
     *
     * @param offset Offset from the start of the buffer. Must be less than the size of the buffer.
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
     * Validates that the offset is within the bounds of the buffer.
     * Throws an exception if the offset is out of bounds.
     */
    void validate(const size_t offset) const;

    TlbWindow* get_cached_tlb_window();

    TLBManager* tlb_manager_;

    // Virtual address of the buffer as the user sees it.
    void* buffer_va_;

    // Size of the buffer requested by the user.
    size_t buffer_size_;

    // Device IO address (system bus) corresponding to user offset 0.
    uint64_t device_io_addr_;

    // NOC address corresponding to user offset 0, when the buffer is NOC-visible.
    std::optional<uint64_t> noc_addr_;

    // Cleanup callback supplied by the owning manager. Invoked from the destructor.
    std::function<void()> on_destroy_;

    std::unique_ptr<TlbWindow> cached_tlb_window = nullptr;
};

}  // namespace tt::umd
