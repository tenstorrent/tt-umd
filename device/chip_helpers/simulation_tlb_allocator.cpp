// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"

#include <fmt/format.h>

#include "tracy.hpp"
#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SimulationTlbAllocator::SimulationTlbAllocator(
    uint64_t bar0_base, tt::ARCH arch, const ArchitectureTlbs* tlbs, uint64_t bar4_base) :
    bar0_base_(bar0_base), bar4_base_(bar4_base), architecture_(arch) {
    initialize_pools(tlbs);
}

int SimulationTlbAllocator::allocate_tlb_index(size_t size) {
    ZoneScopedC(tracy::Color::Cyan);

    // Built without a layout: there are no pools to allocate from, so hand back an
    // auto-incrementing bookkeeping index instead. See allocate_tlb_index()'s docstring.
    if (pools_.empty()) {
        return next_bookkeeping_tlb_id_++;
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
    return bar0_base_ + cfg_reg_base_offset_ + tlb_index * tlb_reg_size_bytes_;
}

bool SimulationTlbAllocator::uses_window_addressing() const { return !pools_.empty(); }

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

void SimulationTlbAllocator::initialize_pools(const ArchitectureTlbs* tlbs) {
    if (tlbs == nullptr) {
        // Intentional: the simulator models no TLB windows for this device, so there is nothing to
        // lay out. Leaving every pool empty is what puts the allocator in bookkeeping-only mode.
        return;
    }

    cfg_reg_base_offset_ = tlbs->static_cfg_addr;
    tlb_reg_size_bytes_ = tlbs->cfg_reg_size_bytes;

    for (const TlbSizeClass& size_class : tlbs->size_classes) {
        TlbPool pool;
        pool.layout = size_class;
        pool.allocated.resize(size_class.count, false);
        pools_.push_back(std::move(pool));
    }
}

}  // namespace tt::umd
