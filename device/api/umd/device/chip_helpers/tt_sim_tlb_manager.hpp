// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <vector>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"

namespace tt::umd {

// Forward declarations to avoid circular dependencies.
class TTSimCommunicator;
class TTSimTTDevice;

class TTSimTlbManager : public TLBManager {
public:
    TTSimTlbManager(TTDevice* tt_device);

    std::unique_ptr<TlbWindow> allocate_tlb_window(
        tlb_data config, const TlbMapping mapping = TlbMapping::WC, const size_t tlb_size = 0);

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
    size_t get_tlb_size_from_index(int tlb_index);

    /**
     * Calculate the address for a TLB based on its index, starting from BAR0 base.
     * @param tlb_index The TLB index
     * @return Address offset from BAR0 base for this TLB
     */
    uint64_t get_tlb_address_from_index(int tlb_index);

    uint64_t get_tlb_reg_address_from_index(int tlb_index);

    /**
     * Get the architecture implementation for architecture-specific operations.
     * @return Pointer to architecture implementation
     */
    const architecture_implementation* get_architecture_impl() const;

    /**
     * Get the TTSimCommunicator for low-level device operations.
     * @return Pointer to TTSimCommunicator
     */
    class TTSimCommunicator* get_communicator() const;

private:
    /**
     * Initialize architecture-specific TLB configuration based on the device architecture.
     */
    void initialize_architecture_config();

    TTSimTTDevice* tt_sim_tt_device_ = nullptr;
    uint64_t bar0_base_ = 0;
    uint64_t tlb_registers_base_ = 0;

    // Architecture-specific TLB configuration.
    tt::ARCH architecture_;
    size_t tlb_reg_size_bytes_ = 8;  // Default to Wormhole size

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
    std::vector<bool> tlb_1mb_allocated_;   // Wormhole: 156 TLBs, Blackhole: 0
    std::vector<bool> tlb_2mb_allocated_;   // Wormhole: 10 TLBs, Blackhole: 202
    std::vector<bool> tlb_16mb_allocated_;  // Wormhole: 20 TLBs, Blackhole: 0
    std::vector<bool> tlb_4gb_allocated_;   // Wormhole: 0 TLBs, Blackhole: 8
};

}  // namespace tt::umd
