// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/io_handle.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

class SimulationTlbAllocator;
enum TlbMapping : uint8_t;

/**
 * Simulation-specific IOHandle that inherits from IOHandle but bypasses hardware operations.
 * This allows compatibility with IOWindow while providing simulation functionality.
 */
class TTSimTlbHandle : public IOHandle {
public:
    /**
     * Create a simulation IOHandle that works with IOWindow.
     * This bypasses the hardware constructor and sets up simulation state.
     */
    static std::unique_ptr<TTSimTlbHandle> create(
        SimulationTlbAllocator* allocator,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping);

    ~TTSimTlbHandle() noexcept;

    void configure(const tlb_data& new_config) override;

    SimulationTlbAllocator* get_tlb_allocator() const { return allocator_; }

    tt::ARCH get_arch() const override;

private:
    // Private constructor to enforce use of create() factory method.
    TTSimTlbHandle(
        SimulationTlbAllocator* allocator,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping);

    void free_tlb() noexcept override;

    SimulationTlbAllocator* allocator_;
    class TTSimCommunicator* sim_communicator_;
    uint64_t tlb_reg_addr_ = 0;
};

}  // namespace tt::umd
