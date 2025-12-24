// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt::umd {

class SimulationSysmemManager : public SysmemManager {
public:
    SimulationSysmemManager(uint32_t num_host_mem_channels);
    ~SimulationSysmemManager();

    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) override;

protected:
    std::vector<uint8_t> system_memory_;
};

}  // namespace tt::umd
