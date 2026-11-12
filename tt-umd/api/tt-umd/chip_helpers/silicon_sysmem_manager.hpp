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

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {
class TTDevice;

class SiliconSysmemManager : public SysmemManager {
public:
    explicit SiliconSysmemManager(TTDevice* tt_device);

    ~SiliconSysmemManager() override;

    // Without an IOMMU each buffer is one 1 GB hugepage, so the size must be at most 1 GB.
    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer,
        size_t sysmem_buffer_size,
        const bool map_to_noc = false,
        DeviceBufferAccess device_access = DeviceBufferAccess::READ_WRITE) override;

    std::optional<size_t> get_max_buffer_count(size_t size) const override;

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
