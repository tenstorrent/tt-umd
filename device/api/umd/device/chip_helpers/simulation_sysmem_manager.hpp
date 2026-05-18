// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {

class SimulationSysmemManager : public SysmemManager {
public:
    SimulationSysmemManager(uint32_t num_host_mem_channels, tt::ARCH arch);
    ~SimulationSysmemManager() override;

    bool pin_or_map_sysmem_to_device() override;

    void unpin_or_unmap_sysmem() override;

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    std::unique_ptr<SysmemBuffer> allocate_sysmem_buffer(
        size_t sysmem_buffer_size, const bool map_to_noc = false) override;

    std::unique_ptr<SysmemBuffer> map_sysmem_buffer(
        void* buffer, size_t sysmem_buffer_size, const bool map_to_noc = false) override;

protected:
    bool init_sysmem(uint32_t num_host_mem_channels) override;

private:
    struct MappedBuffer {
        uint64_t base = 0;
        void* buffer = nullptr;
        size_t size = 0;
    };

    std::optional<MappedBuffer> find_mapped_buffer(uint64_t base, uint32_t size);
    void remove_mapped_buffer(uint64_t base);

    uint8_t* system_memory_ = nullptr;
    size_t system_memory_size_ = 0;
    uint64_t next_mapped_buffer_base_ = 0;
    std::vector<MappedBuffer> mapped_buffers_;
    std::vector<std::pair<void*, size_t>> owned_allocations_;
    std::mutex mapped_buffers_mutex_;
};

}  // namespace tt::umd
