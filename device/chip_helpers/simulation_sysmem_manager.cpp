// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"

#include <fmt/format.h>
#include <sys/mman.h>  // for mmap, munmap
#include <sys/stat.h>  // for fstat

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "assert.hpp"
#include "cpuset_lib.hpp"
#include "hugepage.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"

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

    system_memory_ =
        static_cast<uint8_t*>(mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    TT_ASSERT(system_memory_ != MAP_FAILED, "system_memory mmap() failed");
    madvise(system_memory_, total_size, MADV_HUGEPAGE);
    system_memory_size_ = total_size;

    std::lock_guard<std::mutex> lock(regions_mutex_);
    for (int i = 0; i < num_host_mem_channels; i++) {
        size_t channel_size = (i == 3 && num_host_mem_channels == 4) ? (768 * (1ULL << 20)) : (1ULL << 30);
        uint8_t *channel_va = system_memory_ + i * (1ULL << 30);
        hugepage_mapping_per_channel.push_back({channel_va, channel_size, pcie_base_ + i * (1ULL << 30)});
        // Channel i occupies paddr [i*1GB, i*1GB + channel_size). The simulator strips
        // pcie_base_ before issuing pci_dma callbacks, so paddr space starts at 0.
        paddr_regions_.push_back({i * (1ULL << 30), i * (1ULL << 30) + channel_size, channel_va});
    }
    // Mapped/allocated buffers begin in the first paddr slot not reserved for a channel.
    // We reserve a full 1 GiB slot per channel (matching the simulator's channel layout),
    // even when channel 3 has the 256 MiB carveout — that 256 MiB is left unused so we
    // don't have to special-case the boundary.
    next_alloc_paddr_ = static_cast<uint64_t>(num_host_mem_channels) * (1ULL << 30);

    return true;
}

bool SimulationSysmemManager::pin_or_map_sysmem_to_device() { return true; }

SimulationSysmemManager::~SimulationSysmemManager() { SimulationSysmemManager::unpin_or_unmap_sysmem(); }

void SimulationSysmemManager::unpin_or_unmap_sysmem() {
    {
        std::lock_guard<std::mutex> lock(regions_mutex_);
        paddr_regions_.clear();
    }
    hugepage_mapping_per_channel.clear();
    if (system_memory_ != nullptr) {
        munmap(system_memory_, system_memory_size_);
        system_memory_ = nullptr;
        system_memory_size_ = 0;
    }
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    void *buffer =
        mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (buffer == MAP_FAILED) {
        TT_THROW("Failed to mmap simulation sysmem buffer of size {:#x}.", sysmem_buffer_size);
    }

    uint64_t paddr_start;
    {
        std::lock_guard<std::mutex> lock(regions_mutex_);
        paddr_start = next_alloc_paddr_;
        next_alloc_paddr_ += sysmem_buffer_size;
        paddr_regions_.push_back({paddr_start, paddr_start + sysmem_buffer_size, static_cast<uint8_t *>(buffer)});
    }

    std::optional<uint64_t> noc_addr = map_to_noc ? std::optional<uint64_t>(pcie_base_ + paddr_start) : std::nullopt;

    auto on_destroy = [this, paddr_start, buffer, sysmem_buffer_size]() {
        {
            std::lock_guard<std::mutex> lock(regions_mutex_);
            auto it = std::find_if(paddr_regions_.begin(), paddr_regions_.end(), [paddr_start](const PaddrRegion &r) {
                return r.paddr_start == paddr_start;
            });
            if (it != paddr_regions_.end()) {
                paddr_regions_.erase(it);
            }
        }
        munmap(buffer, sysmem_buffer_size);
    };

    return std::make_unique<SysmemBuffer>(
        nullptr, buffer, sysmem_buffer_size, paddr_start, noc_addr, std::move(on_destroy));
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void *buffer, size_t sysmem_buffer_size, const bool map_to_noc) {
    uint64_t paddr_start;
    {
        std::lock_guard<std::mutex> lock(regions_mutex_);
        paddr_start = next_alloc_paddr_;
        next_alloc_paddr_ += sysmem_buffer_size;
        paddr_regions_.push_back({paddr_start, paddr_start + sysmem_buffer_size, static_cast<uint8_t *>(buffer)});
    }

    std::optional<uint64_t> noc_addr = map_to_noc ? std::optional<uint64_t>(pcie_base_ + paddr_start) : std::nullopt;

    auto on_destroy = [this, paddr_start]() {
        std::lock_guard<std::mutex> lock(regions_mutex_);
        auto it = std::find_if(paddr_regions_.begin(), paddr_regions_.end(), [paddr_start](const PaddrRegion &r) {
            return r.paddr_start == paddr_start;
        });
        if (it != paddr_regions_.end()) {
            paddr_regions_.erase(it);
        }
    };

    return std::make_unique<SysmemBuffer>(
        nullptr, buffer, sysmem_buffer_size, paddr_start, noc_addr, std::move(on_destroy));
}

uint8_t *SimulationSysmemManager::find_paddr_host_va(uint64_t paddr, uint32_t size) {
    std::lock_guard<std::mutex> lock(regions_mutex_);
    for (const auto &r : paddr_regions_) {
        if (paddr >= r.paddr_start && paddr + size <= r.paddr_end) {
            return r.host_va + (paddr - r.paddr_start);
        }
    }
    return nullptr;
}

}  // namespace tt::umd
