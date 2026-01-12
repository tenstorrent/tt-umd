/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {

// Don't use the top 256MB of the 4th hugepage region on WH.  Two reasons:
// 1. There are PCIE PHY registers at the top
// 2. Provision for platform software to have NOC-accessible host memory
static constexpr size_t HUGEPAGE_CHANNEL_3_SIZE_LIMIT = 768 * (1 << 20);

class SiliconSysmemManager : public SysmemManager {
public:
    SiliconSysmemManager(TLBManager* tlb_manager, uint32_t num_host_mem_channels);
    virtual ~SiliconSysmemManager();

    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) override;

protected:
    bool init_sysmem(uint32_t num_host_mem_channels) override;

private:
    bool init_hugepages(uint32_t num_host_mem_channels);

    bool init_iommu(uint32_t num_fake_mem_channels);

    bool pin_or_map_hugepages();
    bool pin_or_map_iommu();

    void print_file_contents(std::string filename, std::string hint = "");
};

}  // namespace tt::umd
