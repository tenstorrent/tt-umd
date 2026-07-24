// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {

class SimulationSysmemManager : public SysmemManager {
public:
    SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch, uint32_t chip_id = 0);
    ~SimulationSysmemManager() override;

    // Per-chip host base for this chip's sysmem. On silicon each chip's pinned host memory lives at a
    // distinct host physical address; UMD programs that address into the chip's outbound iATU as the
    // region target, and the chip's DMA resolves there purely by address (no per-chip "magic"). We
    // model that by giving chip N a distinct host base (N * PER_CHIP_HOST_STRIDE) and using it as the
    // hugepage physical_address (= iATU target). The simulator's host-side DMA router (dma_route) then
    // routes by which chip's [host_base, host_base + PER_CHIP_HOST_STRIDE) window contains the address.
    static constexpr uint64_t PER_CHIP_HOST_STRIDE = 1ULL << 36;  // 64 GiB; >> per-chip sysmem, non-overlapping

    uint64_t get_host_base() const { return host_base_; }

    uint64_t get_host_region_size() const { return PER_CHIP_HOST_STRIDE; }

    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    // Called by TTSimTTDevice::pci_dma_{read,write}_bytes when the simulator
    // fires a DMA callback with a raw device IO address (pcie_base_ + offset).
    //
    // Searches the mapped-buffer registry for a buffer whose device_io_addr
    // range covers [device_io_addr, device_io_addr + size).  On a hit the
    // memcpy is performed and true is returned.  On a miss (the address falls
    // in the regular hugepage arena) false is returned and the caller should
    // forward to the base-class write_to_sysmem / read_from_sysmem with the
    // correct channel and within-channel offset.
    //
    // This keeps the mapped-buffer fast path out of write_to_sysmem /
    // read_from_sysmem so those methods retain the same semantics as the base
    // class (sysmem_dest is a within-channel offset, not an absolute address).
    bool write_mapped_buffer(uint64_t device_io_addr, const void* src, uint32_t size);
    bool read_mapped_buffer(uint64_t device_io_addr, void* dst, uint32_t size);

protected:
    bool init_sysmem(uint32_t num_host_mem_channels) override;

private:
    struct MappedBuffer {
        // Device IO address (pcie_base_ + arena offset) of the first byte.
        uint64_t device_io_addr = 0;
        void* buffer = nullptr;
        size_t size = 0;
    };

    // Shared registry of active mapped-buffer entries.  Held by shared_ptr so
    // that SysmemBuffer unmap callbacks can capture a weak_ptr and safely
    // become no-ops when the manager is destroyed before the buffer.
    struct MappedBufferRegistry {
        std::mutex mutex;
        std::vector<MappedBuffer> buffers;
        // Bump allocator: next arena offset (relative to pcie_base_) to assign.
        uint64_t next_arena_offset = 0;
    };

    // Caller must hold registry_->mutex.
    std::optional<MappedBuffer> find_mapped_buffer_locked(uint64_t device_io_addr, uint32_t size);

    uint8_t* system_memory_ = nullptr;
    size_t system_memory_size_ = 0;
    uint64_t host_base_ = 0;  // this chip's distinct host-physical base (= chip_id * PER_CHIP_HOST_STRIDE)
    std::vector<std::pair<void*, size_t>> owned_allocations_;
    std::shared_ptr<MappedBufferRegistry> registry_;
};

}  // namespace tt::umd
