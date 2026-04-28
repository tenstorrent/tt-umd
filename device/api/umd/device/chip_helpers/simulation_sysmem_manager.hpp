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
    SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch);
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

    uint8_t* system_memory_ = nullptr;
    size_t system_memory_size_ = 0;

    std::mutex regions_mutex_;
    std::vector<PaddrRegion> paddr_regions_;
    // Mapped/allocated SysmemBuffers live immediately above the channel region. The TTSim
    // simulator's translate_pci_dma_addr only handles paddrs in the lower NOC range, so we
    // can't simply park them above the maximum 4-channel layout; we start at the first paddr
    // not occupied by an enabled channel. With 0 channels (the typical TTSim case) this is 0.
    uint64_t next_alloc_paddr_ = 0;
};

}  // namespace tt::umd
