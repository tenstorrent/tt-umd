// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

class SimulationTlbAllocator;
enum TlbMapping : uint8_t;

/**
 * Simulation-specific TlbHandle that inherits from TlbHandle but bypasses hardware operations.
 * This allows compatibility with TlbWindow while providing simulation functionality.
 */
class TTSimTlbHandle : public TlbHandle {
public:
    /**
     * Create a simulation TlbHandle that works with TlbWindow.
     * This bypasses the hardware constructor and sets up simulation state.
     */
    static std::unique_ptr<TTSimTlbHandle> create(
        std::shared_ptr<SimulationTlbAllocator> allocator,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping);

    ~TTSimTlbHandle() noexcept override;

    void configure(const tlb_data& new_config) override;

    SimulationTlbAllocator* get_tlb_allocator() const { return allocator_.get(); }

    tt::ARCH get_arch() const override;

    int export_dmabuf(uint64_t offset = 0, uint64_t size = 0) const override;

private:
    // Private constructor to enforce use of create() factory method.
    TTSimTlbHandle(
        std::shared_ptr<SimulationTlbAllocator> allocator,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping);

    void free_tlb() noexcept override;

    std::shared_ptr<SimulationTlbAllocator> allocator_;
    class TTSimCommunicator* sim_communicator_;
    uint64_t tlb_reg_addr_ = 0;
};

}  // namespace tt::umd
