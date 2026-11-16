/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {
class TTDevice;

// Don't use the top 256MB of the 4th hugepage region on WH.  Two reasons:
// 1. There are PCIE PHY registers at the top
// 2. Provision for platform software to have NOC-accessible host memory
static constexpr size_t HUGEPAGE_CHANNEL_3_SIZE_LIMIT = 768 * (1 << 20);

class SiliconSysmemManager : public SysmemManager {
public:
    SiliconSysmemManager(TTDevice* tt_device, uint32_t num_host_mem_channels);
    ~SiliconSysmemManager() override;

    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer,
        size_t sysmem_buffer_size,
        const bool map_to_noc = false,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) override;

protected:
    bool init_sysmem(uint32_t num_host_mem_channels) override;

private:
    /**
     * Pins buffer_va for the device and wraps it in a SysmemBuffer.
     *
     * release_backing_memory is what distinguishes the two allocation paths: it frees the pages for a buffer
     * this manager allocated, and is empty for one mapped from a caller's pointer.
     */
    std::unique_ptr<SysmemBuffer> pin_and_wrap(
        void* buffer_va,
        size_t buffer_size,
        const bool map_to_noc,
        DeviceBufferAccess device_access,
        SysmemBuffer::Deleter release_backing_memory);

    bool init_hugepages(uint32_t num_host_mem_channels);

    bool init_iommu(uint32_t num_fake_mem_channels);

    bool pin_or_map_hugepages();
    bool pin_or_map_iommu();

    /**
     * Sysmem for DMA-non-coherent hosts (PCIDevice::is_dma_coherent() == false): hugepages and IOMMU-pinned pages
     * are cacheable, so the device and host would see stale data. Channel 0 is instead backed by one uncached KMD DMA
     * buffer (MAX_DMA_BUF_SIZE, 256 MiB), mapped into the NOC window bottom-up at pcie_base_.
     */
    bool init_kmd_dma(uint32_t num_host_mem_channels);
    void* kmd_dma_mapping_ = nullptr;
    size_t kmd_dma_size_ = 0;

    void print_file_contents(const std::string& filename, const std::string& hint = "");
};

}  // namespace tt::umd
