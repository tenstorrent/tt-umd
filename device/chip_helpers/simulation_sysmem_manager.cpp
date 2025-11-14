/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"

#include <sys/mman.h>  // for mmap, munmap
#include <sys/stat.h>  // for fstat

#include <filesystem>
#include <fstream>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "cpuset_lib.hpp"
#include "hugepage.hpp"

namespace tt::umd {

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels) : SysmemManager() {
    init_hugepages(num_host_mem_channels);
}

// SimulationSysmemManager::SimulationSysmemManager() {}

bool SimulationSysmemManager::pin_or_map_sysmem_to_device() { return pin_or_map_hugepages(); }

SimulationSysmemManager::~SimulationSysmemManager() { unpin_or_unmap_sysmem(); }

void SimulationSysmemManager::unpin_or_unmap_sysmem() {}

void SimulationSysmemManager::write_to_sysmem(uint16_t channel, const void *src, uint64_t sysmem_dest, uint32_t size) {
    HugepageMapping hugepage_map = get_hugepage_mapping(channel);
    TT_ASSERT(
        hugepage_map.mapping,
        "write_buffer: Hugepages are not allocated for simulation device ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        channel);

    TT_ASSERT(
        size <= hugepage_map.mapping_size,
        "write_buffer data has larger size {} than destination buffer {}",
        size,
        hugepage_map.mapping_size);
    log_debug(
        LogUMD,
        "Using hugepage mapping at address {:p} offset {} chan {} size {}",
        hugepage_map.mapping,
        (sysmem_dest % hugepage_map.mapping_size),
        channel,
        size);
    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_dest % hugepage_map.mapping_size);

    memcpy(user_scratchspace, src, size);
}

void SimulationSysmemManager::read_from_sysmem(uint16_t channel, void *dest, uint64_t sysmem_src, uint32_t size) {
    HugepageMapping hugepage_map = get_hugepage_mapping(channel);
    TT_ASSERT(
        hugepage_map.mapping,
        "read_buffer: Hugepages are not allocated for simulation device ch: {}."
        " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
        channel);

    void *user_scratchspace = static_cast<char *>(hugepage_map.mapping) + (sysmem_src % hugepage_map.mapping_size);

    log_debug(LogUMD, "Cluster::read_buffer (ch: {}) from {:p}", channel, user_scratchspace);

    memcpy(dest, user_scratchspace, size);
}

bool SimulationSysmemManager::init_hugepages(uint32_t num_host_mem_channels) {
    if (num_host_mem_channels == 0) {
        // No hugepage channels requested, so just skip initialization.
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

    system_memory_.resize(total_size, 0);

    hugepage_mapping_per_channel.resize(num_host_mem_channels);

    for (int i = 0; i < num_host_mem_channels; i++) {
        size_t channel_size = (i == 3 && num_host_mem_channels == 4) ? (768 * (1ULL << 20)) : (1ULL << 30);
        hugepage_mapping_per_channel.push_back({system_memory_.data() + i * (1ULL << 30), channel_size, 0});
    }

    return true;
}

bool SimulationSysmemManager::pin_or_map_hugepages() { return true; }

bool SimulationSysmemManager::init_iommu(uint32_t num_fake_mem_channels) { return true; }

bool SimulationSysmemManager::pin_or_map_iommu() { return true; }

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    return nullptr;
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc) {
    return nullptr;
}

}  // namespace tt::umd
