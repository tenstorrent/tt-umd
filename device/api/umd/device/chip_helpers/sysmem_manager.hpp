// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sysmem_buffer.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_types.hpp"

namespace tt::umd {

class SysmemManager {
public:
    SysmemManager() = default;
    virtual ~SysmemManager() = default;

    virtual void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size);
    virtual void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size);

    /**
     * Further initializes system memory for usage.
     * Includes both hugepage and IOMMU settings, depending on which configuration is enabled.
     * This means different things depending on KMD version:
     * - For KMD version < 2.0.0 this will pin the memory and fill up the device IO address field in the maps
     * which should be used further to program the iatu.
     * - For KMD version >= 2.0.0 this will pin the memory and map it to the device. Device IO address is not
     * needed further by the driver.
     */
    virtual bool pin_or_map_sysmem_to_device() = 0;

    virtual void unpin_or_unmap_sysmem() = 0;

    size_t get_num_host_mem_channels() const;

    HugepageMapping get_hugepage_mapping(size_t channel) const;

    virtual std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) = 0;

    virtual std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) = 0;

protected:
    virtual bool init_sysmem(uint32_t num_host_mem_channels) = 0;

    TLBManager* tlb_manager_ = nullptr;
    TTDevice* tt_device_ = nullptr;
    // const uint64_t pcie_base_;
    uint64_t pcie_base_;

    std::vector<HugepageMapping> hugepage_mapping_per_channel;
    void* iommu_mapping = nullptr;
    size_t iommu_mapping_size = 0;

    std::unique_ptr<SysmemBuffer> sysmem_buffer_ = nullptr;
};

}  // namespace tt::umd
