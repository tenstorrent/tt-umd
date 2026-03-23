// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {

class TTSimCommunicator;

class SimulationSysmemManager : public SysmemManager {
public:
    SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch, TTSimCommunicator* communicator = nullptr);
    ~SimulationSysmemManager() override;

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    void write_to_sysmem_no_clock(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size);
    void read_from_sysmem_no_clock(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size);

    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) override;

protected:
    bool init_sysmem(uint32_t num_host_mem_channels) override;

private:
    TTSimCommunicator* communicator_ = nullptr;
    uint8_t* system_memory_ = nullptr;
    size_t system_memory_size_ = 0;
};

}  // namespace tt::umd
