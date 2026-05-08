// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "umd/device/types/arch.hpp"

namespace tt::umd {

class architecture_implementation;

/**
 * In-process allocator for simulation TLB indices.
 *
 * Tracks which TLB indices are allocated per size class, and computes BAR0-relative
 * addresses for a given index. Counterpart to KMD-managed allocation on silicon —
 * no knowledge of TlbHandle / TlbWindow types.
 */
class SimulationTlbAllocator {
public:
    SimulationTlbAllocator(uint64_t bar0_base, const architecture_implementation* arch_impl);

    /**
     * Allocate the smallest TLB whose size class is >= the requested size. If no
     * TLB is free in that size class, escalate to the next larger size class and
     * retry, continuing until a TLB is allocated or all size classes are exhausted.
     *
     * If size is 0, allocate any available TLB, preferring smaller size classes first.
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

    const architecture_implementation* get_architecture_impl() const;

private:
    // A pool of TLB indices that all share a single size.
    struct TlbSizeClass {
        size_t size = 0;         // Bytes per TLB; 0 means unused on the current arch.
        size_t count = 0;        // Number of TLBs in the pool.
        size_t start_index = 0;  // First global TLB index belonging to this pool.
        std::vector<bool> allocated;
    };

    // Indices into size_classes_; ordered smallest -> largest so allocate-with-escalation
    // and address-layout iteration both Just Work.
    enum SizeClass : size_t { ONE_MB = 0, TWO_MB = 1, SIXTEEN_MB = 2, FOUR_GB = 3, NUM_SIZE_CLASSES = 4 };

    void initialize_architecture_config();

    // Returns the pool that owns `tlb_index`, or nullptr if no pool covers it
    // (including for negative indices).
    TlbSizeClass* find_size_class_for_index(int tlb_index);

    uint64_t bar0_base_ = 0;
    const architecture_implementation* arch_impl_ = nullptr;
    tt::ARCH architecture_;
    size_t tlb_reg_size_bytes_ = 8;  // Default to Wormhole size.

    std::mutex allocation_mutex_;
    std::array<TlbSizeClass, NUM_SIZE_CLASSES> size_classes_;
};

}  // namespace tt::umd
