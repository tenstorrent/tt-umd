// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "sysmem_buffer.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {
class PCIDevice;
class TTDevice;

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

    uint64_t get_pcie_base() const { return pcie_base_; }

    static uint64_t get_pcie_base_for_arch(tt::ARCH arch);

    // Resolve a device IO address to a host pointer for managers that back sysmem
    // with host memory (e.g. SimulationSysmemManager under emule). Returns nullptr
    // by default (silicon managers map to real device IO, not a host pointer).
    virtual uint8_t* resolve_host_ptr(uint64_t /*device_io_addr*/, uint32_t /*size*/) { return nullptr; }

protected:
    virtual bool init_sysmem(uint32_t num_host_mem_channels) = 0;

    PCIDevice* pci_device_ = nullptr;
    TTDevice* tt_device_ = nullptr;
    // TODO: Properly initialize for SimulationSysmemManager.
    uint64_t pcie_base_ = 0;

    std::vector<HugepageMapping> hugepage_mapping_per_channel;
    void* iommu_mapping = nullptr;
    size_t iommu_mapping_size = 0;

    std::unique_ptr<SysmemBuffer> sysmem_buffer_ = nullptr;
};

}  // namespace tt::umd
