// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/types/tlb.hpp"
#include "tt-umd/types/xy_pair.hpp"

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
     * Quasar's inbound route: the TLB_APP_IN0 physical address reaching window-aligned addr on core,
     * or nullopt when the simulator has no inbound route to it.
     */
    using QuasarInboundRoute = std::function<std::optional<uint64_t>(tt_xy_pair core, uint64_t addr)>;

    /**
     * Create a simulation TlbHandle that works with TlbWindow.
     * This bypasses the hardware constructor and sets up simulation state.
     */
    static std::unique_ptr<TTSimTlbHandle> create(
        std::shared_ptr<SimulationTlbAllocator> allocator,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping,
        QuasarInboundRoute quasar_inbound_route = {});

    ~TTSimTlbHandle() noexcept override;

    void configure(const tlb_data& new_config) override;

    SimulationTlbAllocator* get_tlb_allocator() const { return allocator_.get(); }

    tt::ARCH get_arch() const override;

    /**
     * Whether the window as last configured is reached through BAR0. When it is not, accesses go
     * through the simulator's tile API.
     */
    bool is_mapped_through_bar() const { return mapped_through_bar_; }

private:
    // Private constructor to enforce use of create() factory method.
    TTSimTlbHandle(
        std::shared_ptr<SimulationTlbAllocator> allocator,
        class TTSimCommunicator* communicator,
        int tlb_id,
        size_t size,
        const TlbMapping tlb_mapping,
        QuasarInboundRoute quasar_inbound_route);

    void configure_quasar(const tlb_data& new_config);

    void free_tlb() noexcept override;

    std::shared_ptr<SimulationTlbAllocator> allocator_;
    class TTSimCommunicator* sim_communicator_;
    uint64_t tlb_reg_addr_ = 0;
    QuasarInboundRoute quasar_inbound_route_;
    bool mapped_through_bar_ = true;
    // What the TLB_APP_IN0 entry holds, so reconfiguring a window onto the same target writes nothing.
    std::optional<uint64_t> programmed_address_;
};

}  // namespace tt::umd
