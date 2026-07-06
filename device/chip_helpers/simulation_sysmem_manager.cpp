// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"

#include <fmt/format.h>
#include <sys/mman.h>  // for mmap, munmap
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "hugepage.hpp"
#include "tracy.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/utils/error.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {

namespace {

uint64_t align_up(uint64_t value, uint64_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

}  // namespace

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch) {
    pcie_base_ = get_pcie_base_for_arch(arch);
    registry_ = std::make_shared<MappedBufferRegistry>();
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

    // The mapped-buffer arena starts immediately after the hugepage region so
    // that device IO addresses assigned to mapped buffers (pcie_base_ + arena
    // offset) never alias a hugepage channel address (pcie_base_ + channel *
    // 1 GB, for channel in [0, num_channels)).
    registry_->next_arena_offset = total_size;

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
        std::lock_guard<std::mutex> lock(registry_->mutex);
        registry_->buffers.clear();
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

std::optional<SimulationSysmemManager::MappedBuffer> SimulationSysmemManager::find_mapped_buffer_locked(
    uint64_t device_io_addr, uint32_t size) {
    // Caller must hold registry_->mutex.
    for (const auto& b : registry_->buffers) {
        if (device_io_addr >= b.device_io_addr && device_io_addr + size <= b.device_io_addr + b.size) {
            return b;
        }
    }
    return std::nullopt;
}

bool SimulationSysmemManager::write_mapped_buffer(uint64_t device_io_addr, const void* src, uint32_t size) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    auto b = find_mapped_buffer_locked(device_io_addr, size);
    if (!b.has_value()) {
        return false;
    }
    std::memcpy(static_cast<uint8_t*>(b->buffer) + (device_io_addr - b->device_io_addr), src, size);
    return true;
}

bool SimulationSysmemManager::read_mapped_buffer(uint64_t device_io_addr, void* dst, uint32_t size) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    auto b = find_mapped_buffer_locked(device_io_addr, size);
    if (!b.has_value()) {
        return false;
    }
    std::memcpy(dst, static_cast<const uint8_t*>(b->buffer) + (device_io_addr - b->device_io_addr), size);
    return true;
}

uint8_t* SimulationSysmemManager::resolve_host_ptr(uint64_t device_io_addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    auto b = find_mapped_buffer_locked(device_io_addr, size);
    if (!b.has_value()) {
        return nullptr;
    }
    return static_cast<uint8_t*>(b->buffer) + (device_io_addr - b->device_io_addr);
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    void* mapping =
        mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    UMD_ASSERT(mapping != MAP_FAILED, error::RuntimeError, "Simulation sysmem buffer mmap() failed");
    {
        std::lock_guard<std::mutex> lock(registry_->mutex);
        owned_allocations_.push_back({mapping, sysmem_buffer_size});
    }
    return map_sysmem_buffer(mapping, sysmem_buffer_size, map_to_noc);
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc) {
    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t mapped_size = align_up(sysmem_buffer_size, page_size);

    uint64_t device_io_addr = 0;
    {
        std::lock_guard<std::mutex> lock(registry_->mutex);
        const uint64_t arena_offset = align_up(registry_->next_arena_offset, page_size);
        registry_->next_arena_offset = arena_offset + mapped_size;
        device_io_addr = pcie_base_ + arena_offset;
        registry_->buffers.push_back({device_io_addr, buffer, sysmem_buffer_size});
    }

    std::optional<uint64_t> noc_addr = map_to_noc ? std::optional<uint64_t>(device_io_addr) : std::nullopt;

    // Capture a weak_ptr so the unmap callback is a safe no-op if the manager
    // has already been destroyed (unpin_or_unmap_sysmem clears the registry).
    std::weak_ptr<MappedBufferRegistry> weak_reg = registry_;
    return std::make_unique<SysmemBuffer>(
        buffer, sysmem_buffer_size, device_io_addr, noc_addr, [weak_reg, device_io_addr]() {
            if (auto reg = weak_reg.lock()) {
                std::lock_guard<std::mutex> lock(reg->mutex);
                reg->buffers.erase(
                    std::remove_if(
                        reg->buffers.begin(),
                        reg->buffers.end(),
                        [device_io_addr](const SimulationSysmemManager::MappedBuffer& b) {
                            return b.device_io_addr == device_io_addr;
                        }),
                    reg->buffers.end());
            }
        });
}

}  // namespace tt::umd
