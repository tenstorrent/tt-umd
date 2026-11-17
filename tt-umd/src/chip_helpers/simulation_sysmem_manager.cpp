// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/simulation_sysmem_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "common/unsupported.hpp"

namespace tt::umd {

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch, uint32_t chip_id) {
    UMD_NOT_IMPLEMENTED();
}

SimulationSysmemManager::~SimulationSysmemManager() = default;

uint64_t SimulationSysmemManager::get_mapped_arena_size() const { UMD_NOT_IMPLEMENTED(); }

bool SimulationSysmemManager::pin_or_map_sysmem_to_device() { UMD_NOT_IMPLEMENTED(); }

void SimulationSysmemManager::unpin_or_unmap_sysmem() { UMD_NOT_IMPLEMENTED(); }

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_NOT_IMPLEMENTED();
}

bool SimulationSysmemManager::write_mapped_buffer(uint64_t device_io_addr, const void* src, uint32_t size) {
    UMD_NOT_IMPLEMENTED();
}

bool SimulationSysmemManager::read_mapped_buffer(uint64_t device_io_addr, void* dst, uint32_t size) {
    UMD_NOT_IMPLEMENTED();
}

void* SimulationSysmemManager::get_mapped_host_ptr(uint64_t device_io_addr) { UMD_NOT_IMPLEMENTED(); }

bool SimulationSysmemManager::grow_host_mem_channels(uint32_t num_host_mem_channels) { UMD_NOT_IMPLEMENTED(); }

bool SimulationSysmemManager::init_sysmem(uint32_t num_host_mem_channels) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
