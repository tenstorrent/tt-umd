// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>

#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"

namespace tt::umd {

class architecture_implementation;

/**
 * Backend-specific builder that wraps a TlbHandle in the appropriate TlbWindow
 * subclass (e.g. TTSimTlbWindow, RtlSimTlbWindow) and applies the supplied config.
 */
using TlbWindowBuilder =
    std::function<std::unique_ptr<TlbWindow>(std::unique_ptr<TlbHandle> handle, const tlb_data& config)>;

class SimulationTlbManager : public TLBManager {
public:
    SimulationTlbManager(
        TTDevice* tt_device,
        uint64_t bar0_base,
        const architecture_implementation* arch_impl,
        TlbHandleFactory handle_factory,
        TlbWindowBuilder window_builder);

    std::unique_ptr<TlbWindow> allocate_tlb_window(
        tlb_data config, const TlbMapping mapping = TlbMapping::WC, const size_t tlb_size = 0) override;

    /**
     * Allocate a TLB window with a default size based on the device architecture.
     * Returns nullptr if the architecture does not support TLBs.
     */
    std::unique_ptr<TlbWindow> allocate_default_tlb_window();

    SimulationTlbAllocator& get_allocator() { return allocator_; }

private:
    SimulationTlbAllocator allocator_;
    TlbWindowBuilder window_builder_;
};

}  // namespace tt::umd
