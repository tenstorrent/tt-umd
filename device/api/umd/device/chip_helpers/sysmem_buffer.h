/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "umd/device/chip_helpers/tlb_manager.h"
#include "umd/device/tt_xy_pair.h"

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
    // TODO: maybe get rid of map_to_noc, make noc_addr non-optional?
    // "Sysmem" is always mapped to NOC, but this class could be used as a DMA
    // buffer (used by PCIE DMA engine) and not accessed by NOC at all.
    SysmemBuffer(TLBManager* tlb_manager, void* buffer_va, size_t buffer_size, bool map_to_noc = false);
    ~SysmemBuffer();

    void* get_buffer_va() const { return buffer_va; }

    size_t get_buffer_size() const { return buffer_size; }

    uint64_t get_device_io_addr(const size_t offset = 0) const { return device_io_addr + offset; }

    std::optional<uint64_t> get_noc_addr() const { return noc_addr; }

    void dma_write_to_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr);

    void dma_read_from_device(size_t offset, size_t size, tt_xy_pair core, uint64_t addr);

private:
    TLBManager* tlb_manager_;

    // Virtual address in process addr space.
    void* buffer_va;

    size_t buffer_size;

    // Address that is used on the system bus to access the buffer.
    uint64_t device_io_addr;

    // Address that is used on the NOC to access the buffer.  NOC target must be
    // the PCIE core that is connected to the host and this address.
    std::optional<uint64_t> noc_addr;
};

}  // namespace tt::umd
