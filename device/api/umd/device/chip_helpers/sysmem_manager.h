/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "sysmem_buffer.h"
#include "umd/device/chip_helpers/sysmem_buffer.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/cluster_types.h"

namespace tt::umd {

// Don't use the top 256MB of the 4th hugepage region on WH.  Two reasons:
// 1. There are PCIE PHY registers at the top
// 2. Provision for platform software to have NOC-accessible host memory
static constexpr size_t HUGEPAGE_CHANNEL_3_SIZE_LIMIT = 768 * (1 << 20);

class SysmemManager {
public:
    SysmemManager(TLBManager* tlb_manager, uint32_t num_host_mem_channels);
    ~SysmemManager();

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size);
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size);

    /**
     * Further initializes system memory for usage.
     * Includes both hugepage and IOMMU settings, depending on which configuration is enabled.
     * This call will pin the memory and fill up the physical address field in the maps
     * which should be used further to program the iatu.
     */
    bool pin_sysmem_to_device();

    size_t get_num_host_mem_channels() const;
    hugepage_mapping get_hugepage_mapping(size_t channel) const;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(size_t sysmem_buffer_size, const bool map_to_noc = false);

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false);

private:
    /**
     * Allocate sysmem with hugepages.
     */
    bool init_hugepages(uint32_t num_host_mem_channels);
    /**
     * Allocate sysmem without hugepages and map it through IOMMU.
     * This is used when the system is protected by an IOMMU.  The mappings will
     * still appear as hugepages to the caller.
     * @param num_fake_mem_channels number of fake mem channels to allocate
     * @return whether allocation/mapping succeeded.
     */
    bool init_iommu(size_t num_fake_mem_channels);

    bool pin_hugepages();
    bool pin_iommu();

    // For debug purposes when various stages fails.
    void print_file_contents(std::string filename, std::string hint = "");

    TLBManager* tlb_manager_;
    TTDevice* tt_device_;
    const uint64_t pcie_base_;

    std::vector<hugepage_mapping> hugepage_mapping_per_channel;
    void* iommu_mapping = nullptr;
    size_t iommu_mapping_size = 0;

    std::unique_ptr<SysmemBuffer> sysmem_buffer_ = nullptr;
};

}  // namespace tt::umd
