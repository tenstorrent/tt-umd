/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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
     * this manager allocated, and is empty for one mapped from a caller's pointer. A hugepage is pinned as one
     * physically contiguous range, which the KMD always binds to the NOC.
     */
    std::unique_ptr<SysmemBuffer> pin_and_wrap(
        void* buffer_va,
        size_t buffer_size,
        const bool map_to_noc,
        DeviceBufferAccess device_access,
        SysmemBuffer::Deleter release_backing_memory,
        const bool hugepage = false);

    // Without an IOMMU the device can only reach physically contiguous memory, so a buffer is one 1 GB
    // hugepage bound to the NOC.
    std::unique_ptr<SysmemBuffer> allocate_hugepage_buffer(size_t sysmem_buffer_size);

    // Maps the hugepage file with this index, or returns nullptr after logging why it could not.
    void* map_hugepage(const std::string& hugepage_dir, uint16_t index);

    // Maps a free hugepage file; dropping the last handle unmaps the page and frees its file.
    std::shared_ptr<void> acquire_hugepage(const std::string& hugepage_dir);

    bool init_hugepages(uint32_t num_host_mem_channels);

    bool init_iommu(uint32_t num_fake_mem_channels);

    bool pin_or_map_hugepages();
    bool pin_or_map_iommu();

    void print_file_contents(const std::string& filename, const std::string& hint = "");

    struct HugepageFiles;
    std::unique_ptr<HugepageFiles> hugepage_files_;
};

}  // namespace tt::umd
