// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

/**
 * In-process allocator for simulation TLB indices.
 *
 * Tracks which TLB indices are allocated per size class, and computes BAR0-relative
 * addresses for a given index. Counterpart to KMD-managed allocation on silicon —
 * no knowledge of TlbHandle / TlbWindow types.
 */
class SimulationTlbAllocator {
public:
    SimulationTlbAllocator(uint64_t bar0_base, tt::ARCH arch, uint64_t bar4_base = 0);

    /**
     * Allocate the smallest TLB whose size class is >= the requested size. If no
     * TLB is free in that size class, escalate to the next larger size class and
     * retry, continuing until a TLB is allocated or all size classes are exhausted.
     *
     * If size is 0, allocate any available TLB, preferring smaller size classes first.
     *
     * QUASAR has no real TLBs; the pools are empty by design (simulator's communicator
     * handles all I/O underneath). For QUASAR, hand back an auto-incrementing dummy
     * index so window bookkeeping keyed by tlb id does not collide across
     * allocations. Callers should use the requested size directly on QUASAR rather
     * than querying get_tlb_size_from_index() (which has no pool to look up).
     *
     * @param size Requested TLB size in bytes (0 means any available).
     * @return TLB index if successful, -1 if no TLB available.
     */
    int allocate_tlb_index(size_t size);

    /**
     * Mark a TLB index as free.
     */
    void deallocate_tlb_index(int tlb_index);

    /**
     * Size of the TLB at the given index, in bytes.
     */
    size_t get_tlb_size_from_index(int tlb_index);

    /**
     * BAR0-relative address mapped by the TLB at the given index.
     */
    uint64_t get_tlb_address_from_index(int tlb_index);

    /**
     * Address of the TLB configuration register for the given index.
     */
    uint64_t get_tlb_reg_address_from_index(int tlb_index);

    tt::ARCH get_architecture() const;

private:
    // A size class and which of its windows are currently allocated.
    struct TlbPool {
        TlbSizeClass layout;
        std::vector<bool> allocated;
    };

    void initialize_architecture_config();

    // Returns the pool that owns `tlb_index`, or nullptr if no pool covers it
    // (including for negative indices).
    TlbPool* find_pool_for_index(int tlb_index);

    uint64_t bar0_base_ = 0;
    uint64_t bar4_base_ = 0;
    tt::ARCH architecture_;
    size_t tlb_reg_size_bytes_ = 8;  // Default to Wormhole size.

    std::mutex allocation_mutex_;
    // Ordered smallest window size first, so allocate-with-escalation and the BAR0 address
    // layout both follow the iteration order.
    std::vector<TlbPool> pools_;

    // Counter for the Quasar bypass branch of allocate_tlb_index(); see its docstring.
    std::atomic<int> next_bypass_tlb_id_{0};
};

}  // namespace tt::umd
