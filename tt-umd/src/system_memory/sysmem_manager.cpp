// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/system_memory/sysmem_manager.hpp"

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<SysmemBuffer> SysmemManager::allocate_buffer(size_t size, bool bind_to_noc) {
    return allocate_sysmem_buffer(size, bind_to_noc);
}

std::unique_ptr<SysmemBuffer> SysmemManager::map_user_buffer(
    void *user_ptr, size_t size, bool bind_to_noc, DeviceBufferAccess device_access) {
    // Refuse here: without an IOMMU the pin alone still accepts physically contiguous memory.
    UMD_ASSERT(
        is_user_buffer_mapping_supported(),
        error::RuntimeError,
        "This allocator cannot map user buffers; check is_user_buffer_mapping_supported() first.");
    return map_sysmem_buffer(user_ptr, size, bind_to_noc, device_access);
}

bool SysmemManager::is_read_only_page_pinning_supported() const {
    // Managers without a PCI device behind them (simulation) cannot pin host pages at all.
    return pci_device_ != nullptr && pci_device_->is_read_only_page_pinning_supported();
}

}  // namespace tt::umd
