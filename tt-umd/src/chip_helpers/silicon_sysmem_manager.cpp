// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "common/unsupported.hpp"

namespace tt::umd {

SiliconSysmemManager::SiliconSysmemManager(TTDevice* tt_device, uint32_t num_host_mem_channels) {
    UMD_NOT_IMPLEMENTED();
}

SiliconSysmemManager::~SiliconSysmemManager() = default;

bool SiliconSysmemManager::pin_or_map_sysmem_to_device() { UMD_NOT_IMPLEMENTED(); }

void SiliconSysmemManager::unpin_or_unmap_sysmem() { UMD_NOT_IMPLEMENTED(); }

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_NOT_IMPLEMENTED();
}

bool SiliconSysmemManager::init_sysmem(uint32_t num_host_mem_channels) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
