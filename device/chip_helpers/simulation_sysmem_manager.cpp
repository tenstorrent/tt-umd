// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"

#include <fmt/format.h>
#include <sys/mman.h>  // for mmap, munmap
#include <sys/stat.h>  // for fstat
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "tracy.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/utils/error.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {

namespace {

constexpr uint64_t kHostMemChannelSize = 1ULL << 30;

uint64_t align_up(uint64_t value, uint64_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

}  // namespace

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch) {
    pcie_base_ = get_pcie_base_for_arch(arch);
    SimulationSysmemManager::init_sysmem(num_host_mem_channels);
}

bool SimulationSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    ZoneScopedC(tracy::Color::Yellow);
    if (num_host_mem_channels == 0) {
        return true;
    }

    if (num_host_mem_channels > 4) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "SimulationSysmemManager::init_hugepages: num_host_mem_channels {} exceeds max supported 4 channels.",
                num_host_mem_channels));
    }

    uint64_t total_size = num_host_mem_channels * (1ULL << 30);

    if (num_host_mem_channels == 4) {
        total_size -= 256 * (1ULL << 20);
    }

    system_memory_ =
        static_cast<uint8_t*>(mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    UMD_ASSERT(system_memory_ != MAP_FAILED, error::RuntimeError, "system_memory mmap() failed");
    madvise(system_memory_, total_size, MADV_HUGEPAGE);
    system_memory_size_ = total_size;
    next_mapped_buffer_base_ = total_size;

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
    ZoneScopedC(tracy::Color::Yellow);
    {
        std::lock_guard<std::mutex> lock(mapped_buffers_mutex_);
        mapped_buffers_.clear();
    }
    for (const auto& [allocation, allocation_size] : owned_allocations_) {
        munmap(allocation, allocation_size);
    }
    owned_allocations_.clear();
    hugepage_mapping_per_channel.clear();
    if (system_memory_ != nullptr) {
        munmap(system_memory_, system_memory_size_);
        system_memory_ = nullptr;
        system_memory_size_ = 0;
    }
}

std::optional<SimulationSysmemManager::MappedBuffer> SimulationSysmemManager::find_mapped_buffer(
    uint64_t base, uint32_t size) {
    for (const auto& mapped_buffer : mapped_buffers_) {
        if (base >= mapped_buffer.base && base + size <= mapped_buffer.base + mapped_buffer.size) {
            return mapped_buffer;
        }
    }
    return std::nullopt;
}

void SimulationSysmemManager::remove_mapped_buffer(uint64_t base) {
    std::lock_guard<std::mutex> lock(mapped_buffers_mutex_);
    mapped_buffers_.erase(
        std::remove_if(
            mapped_buffers_.begin(),
            mapped_buffers_.end(),
            [base](const MappedBuffer& mapped_buffer) { return mapped_buffer.base == base; }),
        mapped_buffers_.end());
}

void SimulationSysmemManager::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    const uint64_t base = static_cast<uint64_t>(channel) * kHostMemChannelSize + sysmem_dest;
    {
        std::lock_guard<std::mutex> lock(mapped_buffers_mutex_);
        auto mapped_buffer = find_mapped_buffer(base, size);
        if (mapped_buffer.has_value()) {
            std::memcpy(static_cast<uint8_t*>(mapped_buffer->buffer) + (base - mapped_buffer->base), src, size);
            return;
        }
    }

    SysmemManager::write_to_sysmem(channel, src, sysmem_dest, size);
}

void SimulationSysmemManager::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    const uint64_t base = static_cast<uint64_t>(channel) * kHostMemChannelSize + sysmem_src;
    {
        std::lock_guard<std::mutex> lock(mapped_buffers_mutex_);
        auto mapped_buffer = find_mapped_buffer(base, size);
        if (mapped_buffer.has_value()) {
            std::memcpy(dest, static_cast<uint8_t*>(mapped_buffer->buffer) + (base - mapped_buffer->base), size);
            return;
        }
    }

    SysmemManager::read_from_sysmem(channel, dest, sysmem_src, size);
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    void* mapping =
        mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    UMD_ASSERT(mapping != MAP_FAILED, error::RuntimeError, "Simulation sysmem buffer mmap() failed");
    owned_allocations_.push_back({mapping, sysmem_buffer_size});
    return map_sysmem_buffer(mapping, sysmem_buffer_size, map_to_noc);
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc) {
    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t mapped_size = align_up(sysmem_buffer_size, page_size);

    uint64_t mapped_base = 0;
    {
        std::lock_guard<std::mutex> lock(mapped_buffers_mutex_);
        mapped_base = align_up(next_mapped_buffer_base_, page_size);
        next_mapped_buffer_base_ = mapped_base + mapped_size;
        mapped_buffers_.push_back({mapped_base, buffer, sysmem_buffer_size});
    }

    const uint64_t device_io_addr = pcie_base_ + mapped_base;
    std::optional<uint64_t> noc_addr = map_to_noc ? std::optional<uint64_t>(device_io_addr) : std::nullopt;
    return std::make_unique<SysmemBuffer>(buffer, sysmem_buffer_size, device_io_addr, noc_addr, [this, mapped_base]() {
        remove_mapped_buffer(mapped_base);
    });
}

}  // namespace tt::umd
