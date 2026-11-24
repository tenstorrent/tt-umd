// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>

#include "tt-umd/chip_helpers/simulation_tlb_allocator.hpp"
#include "tt-umd/types/arch.hpp"

namespace tt::umd {

// The parts of SimulationTlbAllocator only UMD's simulation devices need, kept off the public API.
class SimulationTlbAllocatorInternal {
public:
    // An allocator whose Quasar windows are configured through the BAR at bar2_base. With bar2_base 0,
    // Quasar has no windows, as with the public constructor.
    static std::shared_ptr<SimulationTlbAllocator> create(
        uint64_t bar0_base, tt::ARCH arch, uint64_t bar4_base, uint64_t bar2_base);

    // Whether the allocator is a Quasar one without windows, leaving all I/O to the simulator's tile API.
    static bool is_bypassed(const SimulationTlbAllocator& allocator);
};

}  // namespace tt::umd
