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

uint64_t mapped_extent(void* buffer, size_t size) {
    static const long page_size = sysconf(_SC_PAGESIZE);
    UMD_ASSERT(page_size > 0 && (page_size & (page_size - 1)) == 0, error::RuntimeError, "Invalid host page size.");
    const uint64_t offset = reinterpret_cast<uintptr_t>(buffer) & (page_size - 1);
    constexpr uint64_t window = SimulationSysmemManager::DEVICE_IO_WINDOW_SIZE;
    UMD_ASSERT(uint64_t(page_size) <= window, error::RuntimeError, "Host page size exceeds the device IO window.");
    UMD_ASSERT(size > 0 && size <= window - offset, error::RuntimeError, "Invalid simulation mapped-buffer size.");
    return (size + offset + page_size - 1) & ~(uint64_t(page_size) - 1);
}

}  // namespace

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch, uint32_t chip_id) {
    // pcie_base_ is the chip-side NOC sysmem-window base (fixed per arch; used for buffers' NOC addresses).
    // host_base_ is this chip's distinct host-physical base -- what UMD programs as the outbound-iATU
    // region target, so each chip's DMA lands in its own host window with no per-chip tag at egress.
    pcie_base_ = get_pcie_base_for_arch(arch);
    host_base_ = static_cast<uint64_t>(chip_id) * PER_CHIP_HOST_STRIDE;
    communication_id_ = static_cast<int>(chip_id);
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

    UMD_ASSERT(total_size <= DEVICE_IO_WINDOW_SIZE, error::RuntimeError, "Sysmem exceeds the device IO window.");

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
        // physical_address is this chip's host base for the channel -- what UMD programs as the outbound
        // iATU region target (host_base_ is per-chip distinct). The chip-side NOC sysmem-window address is
        // separate (get_sysmem_window_noc_base), so the iATU maps NOC-window offset -> this host base.
        hugepage_mapping_per_channel.push_back(
            {system_memory_ + i * (1ULL << 30), channel_size, host_base_ + i * (1ULL << 30)});
    }

    return true;
}

uint64_t SimulationSysmemManager::get_mapped_arena_size() const {
    UMD_ASSERT(
        system_memory_size_ <= DEVICE_IO_WINDOW_SIZE, error::RuntimeError, "Sysmem exceeds the device IO window.");
    return DEVICE_IO_WINDOW_SIZE - system_memory_size_;
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
        if (device_io_addr >= b.device_io_addr && device_io_addr - b.device_io_addr <= b.size &&
            size <= b.size - (device_io_addr - b.device_io_addr)) {
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

void* SimulationSysmemManager::get_mapped_host_ptr(uint64_t device_io_addr) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    // Pure address translation: locate the buffer holding this device IO address (1-byte
    // membership) and return the in-place host pointer. The caller owns the access length, so no
    // range is validated here — read_mapped_buffer/write_mapped_buffer are where a size is checked.
    auto b = find_mapped_buffer_locked(device_io_addr, 1);
    if (!b.has_value()) {
        return nullptr;
    }
    return static_cast<uint8_t*>(b->buffer) + (device_io_addr - b->device_io_addr);
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    const uint64_t extent = mapped_extent(nullptr, sysmem_buffer_size);
    // Hold the arena lock through allocation and registration so an exhaustion failure cannot
    // allocate/populate host memory first, or race another allocation after the capacity check.
    std::lock_guard<std::mutex> lock(registry_->mutex);
    check_arena_capacity(extent);
    void* mapping =
        mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    UMD_ASSERT(mapping != MAP_FAILED, error::RuntimeError, "Simulation sysmem buffer mmap() failed");
    try {
        owned_allocations_.push_back({mapping, sysmem_buffer_size});
        try {
            return map_sysmem_buffer_locked(mapping, sysmem_buffer_size, map_to_noc, DeviceBufferAccess::READ_WRITE);
        } catch (...) {
            owned_allocations_.pop_back();
            throw;
        }
    } catch (...) {
        munmap(mapping, sysmem_buffer_size);
        throw;
    }
}

void SimulationSysmemManager::check_arena_capacity(uint64_t extent) const {
    UMD_ASSERT(
        registry_->next_arena_offset <= DEVICE_IO_WINDOW_SIZE &&
            extent <= DEVICE_IO_WINDOW_SIZE - registry_->next_arena_offset,
        error::RuntimeError,
        "Simulation mapped-buffer arena exhausted.");
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    return map_sysmem_buffer_locked(buffer, sysmem_buffer_size, map_to_noc, device_access);
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer_locked(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_ASSERT(buffer != nullptr, error::RuntimeError, "Cannot map a null simulation buffer.");
    const uint64_t extent = mapped_extent(buffer, sysmem_buffer_size);
    check_arena_capacity(extent);
    static const auto page_size = sysconf(_SC_PAGESIZE);
    const uint64_t host_offset = reinterpret_cast<uintptr_t>(buffer) & (page_size - 1);
    const uint64_t page_io_addr = pcie_base_ + registry_->next_arena_offset;
    const uint64_t device_io_addr = page_io_addr + host_offset;
    const std::optional<uint64_t> noc_addr = map_to_noc ? std::optional<uint64_t>(device_io_addr) : std::nullopt;

    registry_->buffers.push_back({device_io_addr, buffer, sysmem_buffer_size});
    // SysmemBuffer adds the host page offset to its device IO base, but returns noc_addr unchanged.
    // The registry covers only the requested bytes, not the padding on either side of the mapping.
    std::weak_ptr<MappedBufferRegistry> weak_reg = registry_;
    try {
        auto result = std::make_unique<SysmemBuffer>(
            buffer,
            sysmem_buffer_size,
            page_io_addr,
            communication_id_,
            noc_addr,
            [weak_reg, device_io_addr](void*) {
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
            },
            device_access);
        registry_->next_arena_offset += extent;
        return result;
    } catch (...) {
        registry_->buffers.pop_back();
        throw;
    }
}

}  // namespace tt::umd
