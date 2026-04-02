// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

/**
 * Pure TLB allocation and bookkeeping class.
 *
 * Manages TLB index allocation, deallocation, and address computation
 * based on architecture configuration. Has no knowledge of TlbHandle,
 * TlbWindow, or any backend-specific types.
 */
class TlbAllocator {
public:
    TlbAllocator(uint64_t bar0_base, const architecture_implementation* arch_impl);

    /**
     * Allocate a TLB index based on the requested size.
     * @param size Requested TLB size (0 means any available)
     * @return TLB index if successful, -1 if no TLB available
     */
    int allocate_tlb_index(size_t size);

    /**
     * Deallocate a TLB index, making it available for reuse.
     * @param tlb_index The TLB index to deallocate
     */
    void deallocate_tlb_index(int tlb_index);

    /**
     * Get the size of TLB based on its index.
     * @param tlb_index The TLB index
     * @return Size of the TLB in bytes
     */
    size_t get_tlb_size_from_index(int tlb_index) const;

    /**
     * Calculate the address for a TLB based on its index, starting from BAR0 base.
     * @param tlb_index The TLB index
     * @return Address offset from BAR0 base for this TLB
     */
    uint64_t get_tlb_address_from_index(int tlb_index) const;

    /**
     * Calculate the TLB configuration register address for a given index.
     * @param tlb_index The TLB index
     * @return Register address for this TLB
     */
    uint64_t get_tlb_reg_address_from_index(int tlb_index) const;

    /**
     * Get the architecture implementation for architecture-specific operations.
     */
    const architecture_implementation* get_architecture_impl() const;

    /**
     * Get the architecture type.
     */
    tt::ARCH get_architecture() const;

    /**
     * Get the default TLB size for the current architecture.
     * @return Default TLB size in bytes, or 0 if architecture doesn't support TLBs
     */
    size_t get_default_tlb_size() const;

private:
    void initialize_architecture_config();

    uint64_t bar0_base_ = 0;
    const architecture_implementation* arch_impl_ = nullptr;

    tt::ARCH architecture_;
    size_t tlb_reg_size_bytes_ = 8;

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
    std::vector<bool> tlb_1mb_allocated_;
    std::vector<bool> tlb_2mb_allocated_;
    std::vector<bool> tlb_16mb_allocated_;
    std::vector<bool> tlb_4gb_allocated_;
};

}  // namespace tt::umd
