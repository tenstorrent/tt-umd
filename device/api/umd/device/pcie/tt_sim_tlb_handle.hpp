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

class SimulationTlbManager;

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
        SimulationTlbManager* manager,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping);

    ~TTSimTlbHandle() noexcept;

    void configure(const tlb_data& new_config) override;

    SimulationTlbManager* get_tlb_manager() const { return sim_manager_; }

    tt::ARCH get_arch() const override;

private:
    // Private constructor to enforce use of create() factory method.
    TTSimTlbHandle(
        SimulationTlbManager* manager,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping);

    void free_tlb() noexcept override;

    SimulationTlbManager* sim_manager_;
    class TTSimCommunicator* sim_communicator_;
    uint64_t tlb_reg_addr_ = 0;
};

}  // namespace tt::umd
