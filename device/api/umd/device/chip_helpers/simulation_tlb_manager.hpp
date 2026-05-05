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

class SimulationTlbManager;
class TTDevice;
class architecture_implementation;

/**
 * Factory function type for creating TlbWindow instances.
 * Different simulation backends (TTSim, RTL sim) provide their own factory
 * that creates the appropriate TlbHandle + TlbWindow combination.
 */
using TlbWindowFactory = std::function<std::unique_ptr<TlbWindow>(
    SimulationTlbManager* manager, int tlb_id, size_t size, TlbMapping mapping, tlb_data config)>;

class SimulationTlbManager : public TLBManager {
public:
    SimulationTlbManager(
        TTDevice* tt_device,
        uint64_t bar0_base,
        uint64_t bar4_base,
        const architecture_implementation* arch_impl,
        TlbWindowFactory factory);

    std::unique_ptr<TlbWindow> allocate_tlb_window(
        tlb_data config, const TlbMapping mapping = TlbMapping::WC, const size_t tlb_size = 0) override;

    /**
     * Allocate a TLB window with a default size based on the device architecture.
     * Returns nullptr if the architecture does not support TLBs.
     */
    std::unique_ptr<TlbWindow> allocate_default_tlb_window();

    // The methods below forward to the underlying SimulationTlbAllocator. They are
    // exposed on SimulationTlbManager for back-compat with simulation TlbHandle
    // subclasses that hold a SimulationTlbManager* back-pointer.

    int allocate_tlb_index(size_t size);
    void deallocate_tlb_index(int tlb_index);
    size_t get_tlb_size_from_index(int tlb_index);
    uint64_t get_tlb_address_from_index(int tlb_index);
    uint64_t get_tlb_reg_address_from_index(int tlb_index);
    const architecture_implementation* get_architecture_impl() const;

    tt::ARCH get_arch() const;

private:
    SimulationTlbAllocator allocator_;
    TlbWindowFactory factory_;
};

}  // namespace tt::umd
