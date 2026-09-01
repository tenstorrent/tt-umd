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
 *
 * The window layout is handed in at construction rather than derived from the architecture, so
 * supporting a new architecture is a matter of it having an entry in the architecture TLB table.
 */
class SimulationTlbAllocator {
public:
    /**
     * @param bar0_base Base address the architecture's BAR0-resident windows are mapped at.
     * @param arch Architecture of the device this allocator serves. Carried so the TLB handles
     *             built on top of it can answer get_arch(); the allocator never branches on it.
     * @param tlbs Window layout to lay the pools out from, or nullptr when the simulator models no
     *             TLB windows for this device at all (its communicator carries all I/O); see
     *             allocate_tlb_index() for what that mode means.
     * @param bar4_base Base address for the layout's BAR4-resident windows, if it has any.
     */
    SimulationTlbAllocator(uint64_t bar0_base, tt::ARCH arch, const ArchitectureTlbs* tlbs, uint64_t bar4_base = 0);

    /**
     * Allocate the smallest TLB whose size class is >= the requested size. If no
     * TLB is free in that size class, escalate to the next larger size class and
     * retry, continuing until a TLB is allocated or all size classes are exhausted.
     *
     * If size is 0, allocate any available TLB, preferring smaller size classes first.
     *
     * An allocator built without a layout has no pools to allocate from. It hands back an
     * auto-incrementing index instead, so TLBManager bookkeeping (keyed by tlb id) does not
     * collide across allocations. Such an index addresses no window: callers should use the
     * requested size directly rather than querying get_tlb_size_from_index(), which has no pool to
     * look it up in. uses_window_addressing() distinguishes the two modes.
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

    /**
     * Whether the indices this allocator hands out address a real window, i.e. whether it was given
     * a layout. See allocate_tlb_index() for the layout-less mode.
     */
    bool uses_window_addressing() const;

    tt::ARCH get_architecture() const;

private:
    // A size class and which of its windows are currently allocated.
    struct TlbPool {
        TlbSizeClass layout;
        std::vector<bool> allocated;
    };

    void initialize_pools(const ArchitectureTlbs* tlbs);

    // Returns the pool that owns `tlb_index`, or nullptr if no pool covers it
    // (including for negative indices).
    TlbPool* find_pool_for_index(int tlb_index);

    uint64_t bar0_base_ = 0;
    uint64_t bar4_base_ = 0;
    tt::ARCH architecture_;
    // Both taken from the layout. Left at zero without one, where every getter that would use them
    // throws before reaching them.
    uint64_t cfg_reg_base_offset_ = 0;
    uint64_t tlb_reg_size_bytes_ = 0;

    std::mutex allocation_mutex_;
    // Ordered smallest window size first, so allocate-with-escalation and the BAR0 address
    // layout both follow the iteration order.
    std::vector<TlbPool> pools_;

    // Counter for the layout-less branch of allocate_tlb_index(); see its docstring.
    std::atomic<int> next_bookkeeping_tlb_id_{0};
};

}  // namespace tt::umd
