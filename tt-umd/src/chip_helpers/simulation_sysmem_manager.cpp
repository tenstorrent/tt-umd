// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/simulation_sysmem_manager.hpp"

#include <fmt/format.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Channels mirror silicon's 1 GiB hugepage channels, including the shortened fourth one, so code sized for
// silicon sees the same layout.
constexpr uint64_t CHANNEL_SIZE = 1ULL << 30;
constexpr uint32_t MAX_CHANNELS = 4;

uint64_t channel_size(uint32_t channel) { return channel == 3 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT : CHANNEL_SIZE; }

size_t page_size() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }

}  // namespace

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch, uint32_t chip_id) :
    host_base_(static_cast<uint64_t>(chip_id) * PER_CHIP_HOST_STRIDE),
    registry_(std::make_shared<MappedBufferRegistry>()) {
    pcie_base_ = get_pcie_base_for_arch(arch);
    communication_id_ = static_cast<int>(chip_id);
    init_sysmem(num_host_mem_channels);
}

SimulationSysmemManager::~SimulationSysmemManager() { unmap_system_memory(); }

uint64_t SimulationSysmemManager::get_mapped_arena_size() const { return DEVICE_IO_WINDOW_SIZE - system_memory_size_; }

bool SimulationSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    UMD_ASSERT(
        num_host_mem_channels <= MAX_CHANNELS,
        error::RuntimeError,
        fmt::format(
            "{} host memory channels do not fit the {:#x}-byte device IO window; at most {} do.",
            num_host_mem_channels,
            DEVICE_IO_WINDOW_SIZE,
            MAX_CHANNELS));

    size_t total_size = 0;
    for (uint32_t channel = 0; channel < num_host_mem_channels; channel++) {
        total_size += channel_size(channel);
    }

    if (total_size > 0) {
        // Reserved lazily: a simulated channel costs memory only for the pages actually touched.
        void* mapping =
            mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        UMD_ASSERT(
            mapping != MAP_FAILED,
            error::RuntimeError,
            fmt::format("Failed to reserve {:#x} bytes of simulated host memory: {}", total_size, strerror(errno)));
        system_memory_ = static_cast<uint8_t*>(mapping);
    }
    system_memory_size_ = total_size;

    hugepage_mapping_per_channel.clear();
    uint64_t channel_offset = 0;
    for (uint32_t channel = 0; channel < num_host_mem_channels; channel++) {
        hugepage_mapping_per_channel.push_back(
            {system_memory_ + channel_offset, channel_size(channel), host_base_ + channel_offset});
        channel_offset += channel_size(channel);
    }
    return true;
}

void SimulationSysmemManager::unmap_system_memory() {
    if (system_memory_ != nullptr) {
        munmap(system_memory_, system_memory_size_);
    }
    system_memory_ = nullptr;
    system_memory_size_ = 0;
    hugepage_mapping_per_channel.clear();
}

// Simulated host memory is plain process memory the simulator reaches by address, so there is nothing to pin.
bool SimulationSysmemManager::pin_or_map_sysmem_to_device() { return true; }

void SimulationSysmemManager::unpin_or_unmap_sysmem() {}

bool SimulationSysmemManager::grow_host_mem_channels(uint32_t num_host_mem_channels) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    if (num_host_mem_channels <= hugepage_mapping_per_channel.size()) {
        return false;
    }
    // The arena starts right after the channels, so growing them moves every arena address handed out so far.
    UMD_ASSERT(
        registry_->next_arena_offset == 0,
        error::RuntimeError,
        "Host memory channels can only grow before any system memory buffer has been handed out.");
    unmap_system_memory();
    init_sysmem(num_host_mem_channels);
    return true;
}

void SimulationSysmemManager::check_arena_capacity(uint64_t extent) const {
    const uint64_t arena_size = get_mapped_arena_size();
    UMD_ASSERT(
        extent <= arena_size && registry_->next_arena_offset <= arena_size - extent,
        error::RuntimeError,
        fmt::format(
            "A {:#x}-byte mapping does not fit the {:#x} bytes left in the {:#x}-byte mapped-buffer arena.",
            extent,
            arena_size - registry_->next_arena_offset,
            arena_size));
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::register_and_wrap(
    void* buffer,
    size_t sysmem_buffer_size,
    const bool map_to_noc,
    DeviceBufferAccess device_access,
    SysmemBuffer::Deleter release_backing_memory) {
    const SysmemBuffer::AlignedRange range = SysmemBuffer::page_align(buffer, sysmem_buffer_size);
    check_arena_capacity(range.mapped_size);

    const uint64_t aligned_device_io_addr = pcie_base_ + system_memory_size_ + registry_->next_arena_offset;
    const uint64_t device_io_addr = aligned_device_io_addr + range.offset_from_base;
    registry_->buffers.push_back(MappedBuffer{device_io_addr, buffer, sysmem_buffer_size});
    registry_->next_arena_offset += range.mapped_size;

    // The simulated iATU spans the whole device IO window, so a buffer's NOC address is its IOVA and binding is free.
    const std::optional<uint64_t> noc_addr = map_to_noc ? std::optional<uint64_t>(device_io_addr) : std::nullopt;
    SysmemBuffer::NocBinder noc_binder = [device_io_addr]() { return device_io_addr; };

    // The registry is held weakly so a buffer outliving its manager releases its memory without touching it.
    std::weak_ptr<MappedBufferRegistry> registry = registry_;
    SysmemBuffer::Deleter deleter =
        [registry, device_io_addr, release = std::move(release_backing_memory)](void* aligned_start) {
            if (auto live_registry = registry.lock()) {
                std::lock_guard<std::mutex> lock(live_registry->mutex);
                auto& buffers = live_registry->buffers;
                buffers.erase(
                    std::remove_if(
                        buffers.begin(),
                        buffers.end(),
                        [device_io_addr](const MappedBuffer& entry) { return entry.device_io_addr == device_io_addr; }),
                    buffers.end());
            }
            if (release) {
                release(aligned_start);
            }
        };

    return create_buffer(
        /*tt_device=*/nullptr,
        buffer,
        sysmem_buffer_size,
        aligned_device_io_addr,
        communication_id_,
        std::move(deleter),
        noc_addr,
        device_access,
        std::move(noc_binder));
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot allocate an empty system memory buffer.");
    const size_t page = page_size();
    UMD_ASSERT(
        sysmem_buffer_size <= std::numeric_limits<size_t>::max() - (page - 1),
        error::RuntimeError,
        fmt::format("A {:#x}-byte system memory buffer cannot be allocated.", sysmem_buffer_size));
    const size_t mapped_size = (sysmem_buffer_size + page - 1) & ~(page - 1);

    std::lock_guard<std::mutex> lock(registry_->mutex);
    // Checked before mapping so a buffer that cannot be placed costs no memory.
    check_arena_capacity(mapped_size);

    void* memory = mmap(nullptr, mapped_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    UMD_ASSERT(
        memory != MAP_FAILED,
        error::RuntimeError,
        fmt::format("Failed to allocate a {:#x}-byte system memory buffer: {}", mapped_size, strerror(errno)));

    // The buffer owns this mapping, so it stays valid after the manager is gone.
    return register_and_wrap(
        memory, sysmem_buffer_size, map_to_noc, DeviceBufferAccess::READ_WRITE, [mapped_size](void* aligned_start) {
            munmap(aligned_start, mapped_size);
        });
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_ASSERT(buffer != nullptr, error::RuntimeError, "Cannot map a null buffer.");
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot map an empty buffer.");

    std::lock_guard<std::mutex> lock(registry_->mutex);
    // The caller owns the memory, so destroying the buffer only drops it from the registry.
    return register_and_wrap(buffer, sysmem_buffer_size, map_to_noc, device_access, SysmemBuffer::Deleter{});
}

std::optional<SimulationSysmemManager::MappedBuffer> SimulationSysmemManager::find_mapped_buffer_locked(
    uint64_t device_io_addr, uint32_t size) {
    for (const MappedBuffer& entry : registry_->buffers) {
        if (device_io_addr >= entry.device_io_addr && size <= entry.size &&
            device_io_addr - entry.device_io_addr <= entry.size - size) {
            return entry;
        }
    }
    return std::nullopt;
}

bool SimulationSysmemManager::write_mapped_buffer(uint64_t device_io_addr, const void* src, uint32_t size) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    const std::optional<MappedBuffer> entry = find_mapped_buffer_locked(device_io_addr, size);
    if (!entry.has_value()) {
        return false;
    }
    std::memcpy(static_cast<uint8_t*>(entry->buffer) + (device_io_addr - entry->device_io_addr), src, size);
    return true;
}

bool SimulationSysmemManager::read_mapped_buffer(uint64_t device_io_addr, void* dst, uint32_t size) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    const std::optional<MappedBuffer> entry = find_mapped_buffer_locked(device_io_addr, size);
    if (!entry.has_value()) {
        return false;
    }
    std::memcpy(dst, static_cast<const uint8_t*>(entry->buffer) + (device_io_addr - entry->device_io_addr), size);
    return true;
}

void* SimulationSysmemManager::get_mapped_host_ptr(uint64_t device_io_addr) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    const std::optional<MappedBuffer> entry = find_mapped_buffer_locked(device_io_addr, 1);
    if (!entry.has_value()) {
        return nullptr;
    }
    return static_cast<uint8_t*>(entry->buffer) + (device_io_addr - entry->device_io_addr);
}

}  // namespace tt::umd
