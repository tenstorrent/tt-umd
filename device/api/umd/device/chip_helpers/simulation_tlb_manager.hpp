// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {

class architecture_implementation;

/**
 * Factory function type for creating TlbWindow instances.
 * Different simulation backends (TTSim, RTL sim) provide their own factory
 * that creates the appropriate TlbHandle + TlbWindow combination.
 */
using TlbWindowFactory = std::function<std::unique_ptr<TlbWindow>(
    SimulationTlbAllocator* allocator, int tlb_id, size_t size, TlbMapping mapping, tlb_data config)>;

class SimulationTlbManager : public TLBManager {
public:
    SimulationTlbManager(
        TTDevice* tt_device,
        uint64_t bar0_base,
        const architecture_implementation* arch_impl,
        TlbWindowFactory factory);

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
    TlbWindowFactory factory_;
};

}  // namespace tt::umd
