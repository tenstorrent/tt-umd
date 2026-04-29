// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

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
 * no knowledge of IOHandle / IOWindow types.
 */
class SimulationTlbAllocator {
public:
    SimulationTlbAllocator(uint64_t bar0_base, const architecture_implementation* arch_impl);

    /**
     * Allocate a TLB index that fits the requested size. If size is 0, allocate
     * any available TLB, preferring smaller sizes first.
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
    void initialize_architecture_config();

    // Allocate a TLB index assuming allocation_mutex_ is already held by the caller.
    // Used for the size==0 path which recurses internally.
    int allocate_tlb_index_locked(size_t size);

    uint64_t bar0_base_ = 0;
    const architecture_implementation* arch_impl_ = nullptr;

    // Architecture-specific TLB configuration.
    tt::ARCH architecture_;
    size_t tlb_reg_size_bytes_ = 8;  // Default to Wormhole size.

    // TLB size constants (set based on architecture).
    size_t tlb_1mb_size_ = 0;
    size_t tlb_2mb_size_ = 0;
    size_t tlb_16mb_size_ = 0;
    size_t tlb_4gb_size_ = 0;

    // TLB count constants (set based on architecture).
    size_t tlb_1mb_count_ = 0;
    size_t tlb_2mb_count_ = 0;
    size_t tlb_16mb_count_ = 0;
    size_t tlb_4gb_count_ = 0;

    // TLB index ranges (set based on architecture).
    size_t tlb_1mb_start_index_ = 0;
    size_t tlb_2mb_start_index_ = 0;
    size_t tlb_16mb_start_index_ = 0;
    size_t tlb_4gb_start_index_ = 0;

    // TLB allocation tracking (dynamically sized based on architecture).
    std::mutex allocation_mutex_;
    std::vector<bool> tlb_1mb_allocated_;   // Wormhole: 156 TLBs, Blackhole: 0.
    std::vector<bool> tlb_2mb_allocated_;   // Wormhole: 10 TLBs, Blackhole: 202.
    std::vector<bool> tlb_16mb_allocated_;  // Wormhole: 20 TLBs, Blackhole: 0.
    std::vector<bool> tlb_4gb_allocated_;   // Wormhole: 0 TLBs, Blackhole: 8.
};

}  // namespace tt::umd
