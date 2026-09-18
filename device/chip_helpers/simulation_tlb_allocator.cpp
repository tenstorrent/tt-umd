// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"

#include <fmt/format.h>

#include <tt-logger/tt-logger.hpp>

#include "tracy.hpp"
#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SimulationTlbAllocator::SimulationTlbAllocator(uint64_t bar0_base, tt::ARCH arch, uint64_t bar4_base) :
    bar0_base_(bar0_base), bar4_base_(bar4_base), architecture_(arch) {
    initialize_architecture_config();
}

int SimulationTlbAllocator::allocate_tlb_index(size_t size) {
    ZoneScopedC(tracy::Color::Cyan);

    // QUASAR has no real TLBs; the pools are empty by design (simulator's communicator
    // handles all I/O underneath). Hand back an auto-incrementing dummy index so
    // window bookkeeping keyed by tlb id does not collide across allocations.
    if (architecture_ == tt::ARCH::QUASAR) {
        return next_bypass_tlb_id_++;
    }

    std::lock_guard<std::mutex> lock(allocation_mutex_);

    // Walk pools smallest-first; pick the first free slot in the first pool that can
    // satisfy the request. Escalate to a larger pool when the current one is full (a
    // larger TLB still satisfies the request).
    //
    // size == 0 is handled by the same loop: `0 > pool.layout.size` is always false for
    // size_t, so every pool is considered, smallest first.
    for (TlbPool& pool : pools_) {
        if (size > pool.layout.size) {
            continue;
        }
        for (size_t i = 0; i < pool.layout.count; ++i) {
            if (!pool.allocated[i]) {
                pool.allocated[i] = true;
                return static_cast<int>(pool.layout.base_index + i);
            }
        }
    }

    return -1;  // No available TLB.
}

void SimulationTlbAllocator::deallocate_tlb_index(int tlb_index) {
    ZoneScopedC(tracy::Color::Cyan);
    std::lock_guard<std::mutex> lock(allocation_mutex_);
    if (TlbPool* pool = find_pool_for_index(tlb_index)) {
        pool->allocated[static_cast<size_t>(tlb_index) - pool->layout.base_index] = false;
    }
}

size_t SimulationTlbAllocator::get_tlb_size_from_index(int tlb_index) {
    TlbPool* pool = find_pool_for_index(tlb_index);
    if (!pool) {
        UMD_THROW(error::RuntimeError, fmt::format("Invalid simulation TLB index {}.", tlb_index));
    }
    return pool->layout.size;
}

uint64_t SimulationTlbAllocator::get_tlb_address_from_index(int tlb_index) {
    TlbPool* pool = find_pool_for_index(tlb_index);
    if (!pool) {
        UMD_THROW(error::RuntimeError, fmt::format("Invalid simulation TLB index {}.", tlb_index));
    }

    const uint64_t slot = static_cast<uint64_t>(tlb_index) - pool->layout.base_index;

    // Each BAR4 resident window occupies its own region, in TLB-index order.
    if (pool->layout.in_bar4) {
        return bar4_base_ + slot * pool->layout.size;
    }

    // Pools are laid out contiguously in BAR0; sum the sizes of all pools ordered before
    // this one to get the offset where this pool's region begins.
    uint64_t region_offset = 0;
    for (const TlbPool& earlier : pools_) {
        if (&earlier == pool) {
            break;
        }
        if (!earlier.layout.in_bar4) {
            region_offset += earlier.layout.count * earlier.layout.size;
        }
    }

    return bar0_base_ + region_offset + slot * pool->layout.size;
}

uint64_t SimulationTlbAllocator::get_tlb_reg_address_from_index(int tlb_index) {
    if (!find_pool_for_index(tlb_index)) {
        UMD_THROW(error::RuntimeError, fmt::format("Invalid simulation TLB index {}.", tlb_index));
    }
    // TLB configuration registers start at this offset from BAR0 base.
    static constexpr uint64_t TLB_CONFIG_REG_BASE_OFFSET = 0x1fc00000;
    return bar0_base_ + TLB_CONFIG_REG_BASE_OFFSET + tlb_index * tlb_reg_size_bytes_;
}

tt::ARCH SimulationTlbAllocator::get_architecture() const { return architecture_; }

SimulationTlbAllocator::TlbPool* SimulationTlbAllocator::find_pool_for_index(int tlb_index) {
    // Returning nullptr for negative indices avoids signed/unsigned comparison
    // pitfalls below (where size_t promotion would turn -1 into SIZE_MAX).
    if (tlb_index < 0) {
        return nullptr;
    }
    auto idx = static_cast<size_t>(tlb_index);
    for (TlbPool& pool : pools_) {
        if (idx >= pool.layout.base_index && idx < pool.layout.base_index + pool.layout.count) {
            return &pool;
        }
    }
    return nullptr;
}

void SimulationTlbAllocator::initialize_architecture_config() {
    if (architecture_ != tt::ARCH::WORMHOLE_B0 && architecture_ != tt::ARCH::BLACKHOLE) {
        // Intentional: architectures like QUASAR construct a SimulationTlbAllocator
        // but the sim TTDevice's constructor bypasses it entirely (builds the
        // cached TLB window with a fixed index, never calling allocate_tlb_index).
        // Leaving every pool empty is the signal that allocator-driven addressing
        // is not in use.
        log_debug(
            LogUMD,
            fmt::format(
                "Architecture {} does not yet have support for TLB management in simulation. UMD will use legacy "
                "tile_wr_bytes and tile_rd_bytes path.",
                tt::arch_to_str(architecture_)));
        return;
    }

    const ArchitectureTlbs& tlbs = get_architecture_tlbs(architecture_);
    tlb_reg_size_bytes_ = tlbs.cfg_reg_size_bytes;

    for (const TlbSizeClass& size_class : tlbs.size_classes) {
        TlbPool pool;
        pool.layout = size_class;
        pool.allocated.resize(size_class.count, false);
        pools_.push_back(std::move(pool));
    }
}

}  // namespace tt::umd
