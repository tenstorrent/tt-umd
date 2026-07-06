// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "umd/device/pcie/tlb_window.hpp"

namespace tt::umd {

// Forward declaration.
class TTSimCommunicator;
class TlbHandle;
struct tlb_data;

/**
 * Simulation TlbWindow implementation that uses TTSimCommunicator
 * for memory access instead of direct pointer dereferencing.
 * This allows TLB operations to work with TTSim where the device
 * memory is not mapped into the user process.
 */
class TTSimTlbWindow : public TlbWindow {
public:
    TTSimTlbWindow(std::unique_ptr<TlbHandle> handle, TTSimCommunicator* communicator, const tlb_data config = {});

    // Implementation of memory access methods using TTSimCommunicator.
    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write_register(uint64_t offset, const void* data, size_t size) override;
    void read_register(uint64_t offset, void* data, size_t size) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;

    void safe_write16(uint64_t offset, uint16_t value) override;

    uint16_t safe_read16(uint64_t offset) override;

private:
    /**
     * Get the physical address for a TLB window offset.
     * This combines the TLB's base address with the given offset.
     */
    uint64_t get_physical_address(uint64_t offset) const;

    TTSimCommunicator* sim_communicator_;
};

}  // namespace tt::umd
