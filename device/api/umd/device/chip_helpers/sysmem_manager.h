/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/cluster_types.h"

namespace tt::umd {

class SysmemManager {
public:
    SysmemManager(TTDevice* tt_device);
    ~SysmemManager();

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size);
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size);

    bool init_hugepage(uint32_t num_host_mem_channels);
    size_t get_num_host_mem_channels() const;
    hugepage_mapping get_hugepage_mapping(size_t channel) const;

private:
    /**
     * Allocate sysmem without hugepages and map it through IOMMU.
     * This is used when the system is protected by an IOMMU.  The mappings will
     * still appear as hugepages to the caller.
     * @param size sysmem size in bytes; size % (1UL << 30) == 0
     * @return whether allocation/mapping succeeded.
     */
    bool init_iommu(size_t size);

    // For debug purposes when various stages fails.
    void print_file_contents(std::string filename, std::string hint = "");

    TTDevice* tt_device_;

    std::vector<hugepage_mapping> hugepage_mapping_per_channel;
};

}  // namespace tt::umd
