// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {

class SimulationSysmemManager : public SysmemManager {
public:
    SimulationSysmemManager(uint32_t num_host_mem_channels);
    ~SimulationSysmemManager();

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    /**
     * Further initializes system memory for usage.
     * Includes both hugepage and IOMMU settings, depending on which configuration is enabled.
     * This means different things depending on KMD version:
     * - For KMD version < 2.0.0 this will pin the memory and fill up the device IO address field in the maps
     * which should be used further to program the iatu.
     * - For KMD version >= 2.0.0 this will pin the memory and map it to the device. Device IO address is not
     * needed further by the driver.
     */
    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) override;

protected:
    /**
     * Allocate sysmem with hugepages.
     */
    bool init_hugepages(uint32_t num_host_mem_channels) override;
    /**
     * Allocate sysmem without hugepages and map it through IOMMU.
     * This is used when the system is protected by an IOMMU.  The mappings will
     * still appear as hugepages to the caller.
     * @param num_fake_mem_channels number of fake mem channels to allocate
     * @return whether allocation/mapping succeeded.
     */
    bool init_iommu(uint32_t num_fake_mem_channels) override;

    bool pin_or_map_hugepages() override;
    bool pin_or_map_iommu() override;

    std::vector<uint8_t> system_memory_;
};

}  // namespace tt::umd
