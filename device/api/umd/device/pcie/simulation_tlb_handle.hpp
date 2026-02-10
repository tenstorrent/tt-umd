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

class TTSimTlbManager;

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
        TTSimTlbManager* manager, int tlb_id, size_t size, const TlbMapping tlb_mapping);

    ~TTSimTlbHandle() noexcept;

    void configure(const tlb_data& new_config) override;
    uint8_t* get_base() override;
    size_t get_size() const override;
    const tlb_data& get_config() const override;
    TlbMapping get_tlb_mapping() const override;
    int get_tlb_id() const override;

    /**
     * Returns the computed address for this TLB based on BAR0 base + TLB offset.
     * This represents where this TLB would be mapped in the memory space.
     */
    uint64_t get_address() const;

private:
    // Private constructor to enforce use of create() factory method.
    TTSimTlbHandle(TTSimTlbManager* manager, int tlb_id, size_t size, const TlbMapping tlb_mapping);

    void free_tlb() noexcept override;

    TTSimTlbManager* sim_manager_;
    int sim_tlb_id_;
    size_t sim_size_;
    tlb_data sim_config_;
    TlbMapping sim_mapping_;
    uint64_t sim_address_{0};  // Computed address from BAR0 + TLB offset
    uint64_t tlb_reg_addr_ = 0;
};

}  // namespace tt::umd
