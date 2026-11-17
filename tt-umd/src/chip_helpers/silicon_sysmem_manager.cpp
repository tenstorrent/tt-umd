// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

SiliconSysmemManager::SiliconSysmemManager(TTDevice* tt_device, uint32_t num_host_mem_channels) {
    tt_device_ = tt_device;
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::SiliconSysmemManager is not implemented yet");
}

SiliconSysmemManager::~SiliconSysmemManager() = default;

bool SiliconSysmemManager::pin_or_map_sysmem_to_device() {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::pin_or_map_sysmem_to_device is not implemented yet");
}

void SiliconSysmemManager::unpin_or_unmap_sysmem() {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::unpin_or_unmap_sysmem is not implemented yet");
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::allocate_sysmem_buffer is not implemented yet");
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::map_sysmem_buffer is not implemented yet");
}

bool SiliconSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::init_sysmem is not implemented yet");
}

std::unique_ptr<SysmemBuffer> SiliconSysmemManager::pin_and_wrap(
    void* buffer_va,
    size_t buffer_size,
    const bool map_to_noc,
    DeviceBufferAccess device_access,
    SysmemBuffer::Deleter release_backing_memory) {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::pin_and_wrap is not implemented yet");
}

bool SiliconSysmemManager::init_hugepages(uint32_t num_host_mem_channels) {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::init_hugepages is not implemented yet");
}

bool SiliconSysmemManager::init_iommu(uint32_t num_fake_mem_channels) {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::init_iommu is not implemented yet");
}

bool SiliconSysmemManager::pin_or_map_hugepages() {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::pin_or_map_hugepages is not implemented yet");
}

bool SiliconSysmemManager::pin_or_map_iommu() {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::pin_or_map_iommu is not implemented yet");
}

void SiliconSysmemManager::print_file_contents(const std::string& filename, const std::string& hint) {
    UMD_THROW(error::RuntimeError, "SiliconSysmemManager::print_file_contents is not implemented yet");
}

}  // namespace tt::umd
