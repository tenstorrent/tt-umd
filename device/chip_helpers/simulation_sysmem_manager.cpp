// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"

#include <sys/mman.h>  // for mmap, munmap
#include <sys/stat.h>  // for fstat

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "cpuset_lib.hpp"
#include "hugepage.hpp"

namespace tt::umd {

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels) {
    SimulationSysmemManager::init_sysmem(num_host_mem_channels);
}

bool SimulationSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    if (num_host_mem_channels == 0) {
        return true;
    }

    if (num_host_mem_channels > 4) {
        TT_THROW(
            "SimulationSysmemManager::init_hugepages: num_host_mem_channels {} exceeds max supported 4 channels.",
            num_host_mem_channels);
    }

    uint64_t total_size = num_host_mem_channels * (1ULL << 30);

    if (num_host_mem_channels == 4) {
        total_size -= 256 * (1ULL << 20);
    }

    system_memory_ = (uint8_t*)mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TT_ASSERT(system_memory_ != MAP_FAILED, "system_memory mmap() failed");
    system_memory_size_ = total_size;

    for (int i = 0; i < num_host_mem_channels; i++) {
        size_t channel_size = (i == 3 && num_host_mem_channels == 4) ? (768 * (1ULL << 20)) : (1ULL << 30);
        hugepage_mapping_per_channel.push_back(
            {system_memory_ + i * (1ULL << 30), channel_size, pcie_base_ + i * (1ULL << 30)});
    }

    return true;
}

bool SimulationSysmemManager::pin_or_map_sysmem_to_device() { return true; }

SimulationSysmemManager::~SimulationSysmemManager() { SimulationSysmemManager::unpin_or_unmap_sysmem(); }

void SimulationSysmemManager::unpin_or_unmap_sysmem() {
    hugepage_mapping_per_channel.clear();
    if (system_memory_ != nullptr) {
        munmap(system_memory_, system_memory_size_);
        system_memory_ = nullptr;
        system_memory_size_ = 0;
    }
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    return nullptr;
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc) {
    return nullptr;
}

}  // namespace tt::umd
