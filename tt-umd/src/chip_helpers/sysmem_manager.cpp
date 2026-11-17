// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/sysmem_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "common/unsupported.hpp"

namespace tt::umd {

std::unique_ptr<SysmemBuffer> SysmemManager::allocate_buffer(size_t size, bool bind_to_noc) {
    return allocate_sysmem_buffer(size, bind_to_noc);
}

std::unique_ptr<SysmemBuffer> SysmemManager::map_user_buffer(
    void* user_ptr, size_t size, bool bind_to_noc, DeviceBufferAccess device_access) {
    return map_sysmem_buffer(user_ptr, size, bind_to_noc, device_access);
}

void SysmemManager::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    UMD_NOT_IMPLEMENTED();
}

void SysmemManager::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    UMD_NOT_IMPLEMENTED();
}

size_t SysmemManager::get_num_host_mem_channels() const { UMD_NOT_IMPLEMENTED(); }

HugepageMapping SysmemManager::get_hugepage_mapping(size_t channel) const { UMD_NOT_IMPLEMENTED(); }

bool SysmemManager::is_read_only_page_pinning_supported() const { UMD_NOT_IMPLEMENTED(); }

uint64_t SysmemManager::get_pcie_base_for_arch(tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
