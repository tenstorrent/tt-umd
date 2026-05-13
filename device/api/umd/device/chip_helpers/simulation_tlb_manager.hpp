// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
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
    std::shared_ptr<SimulationTlbAllocator> allocator, int tlb_id, size_t size, TlbMapping mapping, tlb_data config)>;

class SimulationTlbManager : public TLBManager {
public:
    // bar4_base is trailing with a default of 0 so callers that don't use the 4GB-TLB BAR4
    // path (Wormhole, RTL sim, and any downstream that predates this parameter) keep working.
    SimulationTlbManager(
        TTDevice* tt_device,
        uint64_t bar0_base,
        const architecture_implementation* arch_impl,
        TlbWindowFactory factory,
        uint64_t bar4_base = 0);

    std::unique_ptr<TlbWindow> allocate_tlb_window(
        tlb_data config, const TlbMapping mapping = TlbMapping::WC, const size_t tlb_size = 0) override;

    /**
     * Allocate a TLB window with a default size based on the device architecture.
     * Returns nullptr if the architecture does not support TLBs.
     */
    std::unique_ptr<TlbWindow> allocate_default_tlb_window();

    SimulationTlbAllocator& get_allocator() { return *allocator_; }

private:
    // Held as shared_ptr so TlbHandles can co-own the allocator and outlive the
    // manager — required because Metal's destruction order can destroy the
    // manager before the handles that still reference its allocator.
    std::shared_ptr<SimulationTlbAllocator> allocator_;
    TlbWindowFactory factory_;

    // Monotonic TLB id handed out for architectures that bypass the allocator
    // (Quasar). Always increases — never reused — so TlbHandle::get_tlb_id()
    // is unique across the lifetime of the manager.
    std::atomic<int> next_bypass_tlb_id_{0};
};

}  // namespace tt::umd
