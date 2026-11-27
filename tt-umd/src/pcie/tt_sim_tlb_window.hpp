// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/types/io_window_config.hpp"

namespace tt::umd {

// Forward declaration.
class TTSimCommunicator;
class TlbHandle;

/**
 * IoWindow over a simulated TLB that uses TTSimCommunicator for memory access instead of direct pointer
 * dereferencing. This allows TLB operations to work with TTSim where the device memory is not mapped into
 * the user process.
 *
 * configure() honors no WindowFlags beyond the direction and accepts every ordering.
 */
class TTSimTlbWindow : public IoWindow {
public:
    TTSimTlbWindow(std::unique_ptr<TlbHandle> handle, TTSimCommunicator* communicator);

    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write64(uint64_t offset, uint64_t value) override;
    uint64_t read64(uint64_t offset) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;
    void write_aligned(uint64_t offset, const void* data, size_t size) override;
    void read_aligned(uint64_t offset, void* data, size_t size) override;
    void configure(const TargetIoWindowConfig& config) override;
    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override;
    TargetIoWindowConfig get_target_config() const override;
    IoOrdering get_io_ordering() const override;
    size_t get_size() const override;
    HostMemoryCaching get_memory_caching_type() const override;

private:
    /**
     * Get the physical address for a TLB window offset.
     * This combines the TLB's base address with the given offset.
     */
    uint64_t get_physical_address(uint64_t offset) const;

    std::unique_ptr<TlbHandle> tlb_handle_;
    // Distance from the TLB-size aligned address the entry maps to the window base.
    uint64_t offset_from_aligned_addr_ = 0;
    TTSimCommunicator* sim_communicator_;
};

}  // namespace tt::umd
