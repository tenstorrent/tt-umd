// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {

class SimulationSysmemManager : public SysmemManager {
public:
    SimulationSysmemManager(uint32_t num_host_mem_channels);
    ~SimulationSysmemManager() override;

    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    /**
     * Routes a paddr (PCIe-core-local address presented by the TTSim simulator on
     * device-initiated NOC<->PCIe transactions) to the host VA that backs that range.
     *
     * Looks across both the channel-region mappings populated in init_sysmem and any
     * mapped/allocated SysmemBuffers that have been registered via this manager.
     *
     * @param paddr Start of the requested transfer in PCIe-core-local address space.
     * @param size  Size of the transfer in bytes. The full range [paddr, paddr+size) must lie
     *              within a single registered region; otherwise nullptr is returned.
     * @return Host VA corresponding to paddr, or nullptr if no region covers the range.
     */
    uint8_t* find_paddr_host_va(uint64_t paddr, uint32_t size);

protected:
    bool init_sysmem(uint32_t num_host_mem_channels) override;

private:
    struct PaddrRegion {
        uint64_t paddr_start;
        uint64_t paddr_end;  // exclusive
        uint8_t* host_va;
    };

    // Mapped/allocated SysmemBuffers live above the maximum 4-channel layout so they can never
    // collide with channel regions, regardless of how many channels were requested.
    static constexpr uint64_t kMappedBufferRegionBase = 4ULL << 30;

    uint8_t* system_memory_ = nullptr;
    size_t system_memory_size_ = 0;

    std::mutex regions_mutex_;
    std::vector<PaddrRegion> paddr_regions_;
    uint64_t next_alloc_paddr_ = kMappedBufferRegionBase;
};

}  // namespace tt::umd
