// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/simulation_sysmem_manager.hpp"

#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "hugepage.hpp"
#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Channels are laid out like silicon hugepages: channel i starts at i * HUGEPAGE_REGION_SIZE, and only the
// last of the four channels is cut short.
uint64_t channel_size(uint32_t channel) {
    return channel == MAX_HOST_MEM_CHANNELS - 1 ? HUGEPAGE_CHANNEL_3_SIZE_LIMIT : HUGEPAGE_REGION_SIZE;
}

}  // namespace

SimulationSysmemManager::SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch, uint32_t chip_id) :
    host_base_(chip_id * PER_CHIP_HOST_STRIDE), registry_(std::make_shared<MappedBufferRegistry>()) {
    pcie_base_ = get_pcie_base_for_arch(arch);
    communication_id_ = static_cast<int>(chip_id);
    init_sysmem(num_host_mem_channels);
}

SimulationSysmemManager::~SimulationSysmemManager() { unmap_system_memory(); }

uint64_t SimulationSysmemManager::get_mapped_arena_size() const { return DEVICE_IO_WINDOW_SIZE - system_memory_size_; }

// The simulator reaches host memory by address, so there is nothing to pin: the channels are usable from
// construction until the manager is destroyed.
bool SimulationSysmemManager::pin_or_map_sysmem_to_device() { return true; }

void SimulationSysmemManager::unpin_or_unmap_sysmem() {}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::allocate_sysmem_buffer(
    size_t sysmem_buffer_size, const bool map_to_noc) {
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot allocate an empty sysmem buffer.");
    void* memory =
        mmap(nullptr, sysmem_buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    UMD_ASSERT(
        memory != MAP_FAILED,
        error::RuntimeError,
        "Allocating a sysmem buffer of " + std::to_string(sysmem_buffer_size) +
            " bytes failed: " + std::strerror(errno));

    SysmemBuffer::Deleter release = [sysmem_buffer_size](void* base) { munmap(base, sysmem_buffer_size); };
    try {
        return register_and_wrap(memory, sysmem_buffer_size, map_to_noc, DeviceBufferAccess::READ_WRITE, release);
    } catch (...) {
        release(memory);
        throw;
    }
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::map_sysmem_buffer(
    void* buffer, size_t sysmem_buffer_size, const bool map_to_noc, DeviceBufferAccess device_access) {
    UMD_ASSERT(buffer != nullptr, error::RuntimeError, "Cannot map a null sysmem buffer.");
    UMD_ASSERT(sysmem_buffer_size > 0, error::RuntimeError, "Cannot map an empty sysmem buffer.");
    return register_and_wrap(buffer, sysmem_buffer_size, map_to_noc, device_access, SysmemBuffer::Deleter{});
}

bool SimulationSysmemManager::write_mapped_buffer(uint64_t device_io_addr, const void* src, uint32_t size) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    const std::optional<MappedBuffer> hit = find_mapped_buffer_locked(device_io_addr, size);
    if (!hit) {
        return false;
    }
    // Copied under the lock, so the buffer cannot be released mid-copy.
    std::memcpy(static_cast<uint8_t*>(hit->buffer) + (device_io_addr - hit->device_io_addr), src, size);
    return true;
}

bool SimulationSysmemManager::read_mapped_buffer(uint64_t device_io_addr, void* dst, uint32_t size) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    const std::optional<MappedBuffer> hit = find_mapped_buffer_locked(device_io_addr, size);
    if (!hit) {
        return false;
    }
    std::memcpy(dst, static_cast<const uint8_t*>(hit->buffer) + (device_io_addr - hit->device_io_addr), size);
    return true;
}

void* SimulationSysmemManager::get_mapped_host_ptr(uint64_t device_io_addr) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    const std::optional<MappedBuffer> hit = find_mapped_buffer_locked(device_io_addr, 1);
    if (!hit) {
        return nullptr;
    }
    return static_cast<uint8_t*>(hit->buffer) + (device_io_addr - hit->device_io_addr);
}

bool SimulationSysmemManager::grow_host_mem_channels(uint32_t num_host_mem_channels) {
    std::lock_guard<std::mutex> lock(registry_->mutex);
    if (num_host_mem_channels <= hugepage_mapping_per_channel.size()) {
        return false;
    }
    // Mapped buffers sit right after the channels in the device IO window, so their addresses would move.
    UMD_ASSERT(
        registry_->buffers.empty(),
        error::RuntimeError,
        "Cannot grow host memory channels while sysmem buffers are mapped.");
    unmap_system_memory();
    init_sysmem(num_host_mem_channels);
    registry_->next_arena_offset = 0;
    return true;
}

bool SimulationSysmemManager::init_sysmem(uint32_t num_host_mem_channels) {
    UMD_ASSERT(
        num_host_mem_channels <= MAX_HOST_MEM_CHANNELS,
        error::RuntimeError,
        "At most " + std::to_string(MAX_HOST_MEM_CHANNELS) + " host memory channels are supported, " +
            std::to_string(num_host_mem_channels) + " requested.");
    if (num_host_mem_channels == 0) {
        return true;
    }

    const uint64_t total_size =
        (num_host_mem_channels - 1) * HUGEPAGE_REGION_SIZE + channel_size(num_host_mem_channels - 1);
    // NORESERVE: channels are mostly untouched, so only the pages a test or simulator writes get backed.
    void* memory =
        mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    UMD_ASSERT(
        memory != MAP_FAILED,
        error::RuntimeError,
        "Mapping " + std::to_string(num_host_mem_channels) +
            " simulated host memory channels failed: " + std::strerror(errno));

    system_memory_ = static_cast<uint8_t*>(memory);
    system_memory_size_ = total_size;
    for (uint32_t channel = 0; channel < num_host_mem_channels; channel++) {
        const uint64_t channel_offset = channel * HUGEPAGE_REGION_SIZE;
        hugepage_mapping_per_channel.push_back(
            HugepageMapping{system_memory_ + channel_offset, channel_size(channel), host_base_ + channel_offset});
    }
    log_debug(
        LogUMD,
        "Simulated sysmem for chip {}: {} channels, host base {:#x}.",
        communication_id_,
        num_host_mem_channels,
        host_base_);
    return true;
}

void SimulationSysmemManager::check_arena_capacity(uint64_t extent) const {
    // next_arena_offset never exceeds the arena size, so the subtraction cannot wrap.
    UMD_ASSERT(
        extent <= get_mapped_arena_size() - registry_->next_arena_offset,
        error::RuntimeError,
        "Sysmem buffer of " + std::to_string(extent) + " mapped bytes does not fit in the " +
            std::to_string(get_mapped_arena_size() - registry_->next_arena_offset) +
            " bytes left in the device IO window.");
}

void SimulationSysmemManager::unmap_system_memory() {
    if (system_memory_ != nullptr) {
        munmap(system_memory_, system_memory_size_);
    }
    system_memory_ = nullptr;
    system_memory_size_ = 0;
    hugepage_mapping_per_channel.clear();
}

std::optional<SimulationSysmemManager::MappedBuffer> SimulationSysmemManager::find_mapped_buffer_locked(
    uint64_t device_io_addr, uint32_t size) {
    // Compared by distance from each buffer's start, so an address near the top of the range cannot wrap.
    const auto covers = [device_io_addr, size](const MappedBuffer& mapped) {
        if (device_io_addr < mapped.device_io_addr) {
            return false;
        }
        const uint64_t offset = device_io_addr - mapped.device_io_addr;
        return offset < mapped.size && size <= mapped.size - offset;
    };
    const auto it = std::find_if(registry_->buffers.begin(), registry_->buffers.end(), covers);
    if (it == registry_->buffers.end()) {
        return std::nullopt;
    }
    return *it;
}

std::unique_ptr<SysmemBuffer> SimulationSysmemManager::register_and_wrap(
    void* buffer,
    size_t sysmem_buffer_size,
    const bool map_to_noc,
    DeviceBufferAccess device_access,
    SysmemBuffer::Deleter release_backing_memory) {
    const SysmemBuffer::AlignedRange aligned = SysmemBuffer::page_align(buffer, sysmem_buffer_size);

    std::lock_guard<std::mutex> lock(registry_->mutex);
    check_arena_capacity(aligned.mapped_size);
    const uint64_t aligned_device_io_addr = pcie_base_ + system_memory_size_ + registry_->next_arena_offset;
    const uint64_t device_io_addr = aligned_device_io_addr + aligned.offset_from_base;
    registry_->next_arena_offset += aligned.mapped_size;
    registry_->buffers.push_back(MappedBuffer{device_io_addr, buffer, sysmem_buffer_size});

    // The simulator routes by address alone, so the NOC address of a buffer is its device IO address.
    const std::optional<uint64_t> noc_addr = map_to_noc ? std::optional<uint64_t>(device_io_addr) : std::nullopt;
    const SysmemBuffer::NocBinder noc_binder = [device_io_addr]() { return device_io_addr; };

    // Unregisters before releasing, so a lookup never returns memory that is being freed. The registry is
    // held weakly because a buffer may outlive its manager, and then there is nothing left to unregister from.
    SysmemBuffer::Deleter deleter = [registry = std::weak_ptr<MappedBufferRegistry>(registry_),
                                     device_io_addr,
                                     release = std::move(release_backing_memory)](void* aligned_base) {
        if (const std::shared_ptr<MappedBufferRegistry> live_registry = registry.lock()) {
            std::lock_guard<std::mutex> lock(live_registry->mutex);
            auto& buffers = live_registry->buffers;
            buffers.erase(
                std::remove_if(
                    buffers.begin(),
                    buffers.end(),
                    [device_io_addr](const MappedBuffer& mapped) { return mapped.device_io_addr == device_io_addr; }),
                buffers.end());
            // Bump allocation never reuses a hole, but once nothing is mapped the whole arena is free again.
            if (buffers.empty()) {
                live_registry->next_arena_offset = 0;
            }
        }
        if (release) {
            release(aligned_base);
        }
    };

    log_trace(
        LogUMD,
        "Mapped sysmem buffer {} ({} bytes) at device IO address {:#x}.",
        buffer,
        sysmem_buffer_size,
        device_io_addr);
    return create_buffer(
        /*tt_device=*/nullptr,
        buffer,
        sysmem_buffer_size,
        aligned_device_io_addr,
        communication_id_,
        std::move(deleter),
        noc_addr,
        device_access,
        noc_binder);
}

}  // namespace tt::umd
