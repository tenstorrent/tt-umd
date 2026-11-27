/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "tt-umd/system_memory/sysmem_manager.hpp"

namespace tt::umd {
class PCIDevice;

class SiliconSysmemManager : public SysmemManager {
public:
    /**
     * @param pci_device Device the buffers are pinned for. Must outlive this manager and its buffers.
     * @param communication_id Identifier of that device, stamped into every buffer.
     */
    SiliconSysmemManager(PCIDevice* pci_device, int communication_id);

    // Without an IOMMU each buffer is one 1 GB hugepage, so the size must be at most 1 GB.
    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer,
        size_t sysmem_buffer_size,
        const bool map_to_noc = false,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) override;

    std::optional<size_t> get_max_buffer_count(size_t size) const override;

    bool is_user_buffer_mapping_supported() const override;

    /**
     * Allocates the memory of host memory channel `channel`, bound to the NOC. With an IOMMU, size may also cover the
     * channels after it, so one buffer can back several channels. Without an IOMMU it is the device's hugepage for
     * that channel, the same page every process using the device maps for that channel, so size must be at most 1 GB.
     */
    std::unique_ptr<SysmemBuffer> allocate_channel_buffer(uint32_t channel, size_t size);

private:
    /**
     * Pins buffer_va for the device and wraps it in a SysmemBuffer.
     *
     * release_backing_memory is what distinguishes the two allocation paths: it frees the pages for a buffer
     * this manager allocated, and is empty for one mapped from a caller's pointer. hugepage says buffer_va lies
     * in one physically contiguous hugepage, so it can be bound to the NOC without an IOMMU.
     */
    std::unique_ptr<SysmemBuffer> pin_and_wrap(
        void* buffer_va,
        size_t buffer_size,
        const bool map_to_noc,
        DeviceBufferAccess device_access,
        SysmemBuffer::Deleter release_backing_memory,
        const bool hugepage = false);

    void print_file_contents(const std::string& filename, const std::string& hint = "");
};

}  // namespace tt::umd
