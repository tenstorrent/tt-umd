// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"

#include <fmt/format.h>

#include <tt-logger/tt-logger.hpp>

#include "tracy.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SimulationTlbAllocator::SimulationTlbAllocator(
    uint64_t bar0_base, const architecture_implementation* arch_impl, uint64_t bar4_base) :
    bar0_base_(bar0_base), bar4_base_(bar4_base), arch_impl_(arch_impl) {
    initialize_architecture_config();
}

int SimulationTlbAllocator::allocate_tlb_index(size_t size) {
    ZoneScopedC(tracy::Color::Cyan);

    // QUASAR has no real TLBs; the pools are empty by design (simulator's communicator
    // handles all I/O underneath). Hand back an auto-incrementing dummy index so
    // TLBManager bookkeeping (keyed by tlb id) does not collide across allocations.
    if (architecture_ == tt::ARCH::QUASAR) {
        return next_bypass_tlb_id_++;
    }

    std::lock_guard<std::mutex> lock(allocation_mutex_);

    // Walk size classes smallest-first; pick the first free slot in the first
    // class that can satisfy the request. Escalate to a larger class when the
    // current one is full (a larger TLB still satisfies the request).
    //
    // size == 0 is handled by the same loop: `0 > sc.size` is always false for
    // size_t, so every non-empty class is considered, smallest first.
    for (auto& sc : size_classes_) {
        if (sc.size == 0 || size > sc.size) {
            continue;
        }
        for (size_t i = 0; i < sc.count; ++i) {
            if (!sc.allocated[i]) {
                sc.allocated[i] = true;
                return static_cast<int>(sc.start_index + i);
            }
        }
    }

    return -1;  // No available TLB.
}

void SimulationTlbAllocator::deallocate_tlb_index(int tlb_index) {
    ZoneScopedC(tracy::Color::Cyan);
    std::lock_guard<std::mutex> lock(allocation_mutex_);
    if (auto* sc = find_size_class_for_index(tlb_index)) {
        sc->allocated[static_cast<size_t>(tlb_index) - sc->start_index] = false;
    }
}

size_t SimulationTlbAllocator::get_tlb_size_from_index(int tlb_index) {
    auto* sc = find_size_class_for_index(tlb_index);
    if (!sc) {
        UMD_THROW(error::RuntimeError, fmt::format("Invalid simulation TLB index {}.", tlb_index));
    }
    return sc->size;
}

uint64_t SimulationTlbAllocator::get_tlb_address_from_index(int tlb_index) {
    auto* sc = find_size_class_for_index(tlb_index);
    if (!sc) {
        UMD_THROW(error::RuntimeError, fmt::format("Invalid simulation TLB index {}.", tlb_index));
    }

    // BH 4GB TLBs live in BAR4, not BAR0. Each 4GB slot occupies its own region in
    // BAR4, in TLB-index order starting from the FOUR_GB pool's start_index.
    if (architecture_ == tt::ARCH::BLACKHOLE && sc == &size_classes_[FOUR_GB]) {
        uint64_t slot = static_cast<uint64_t>(tlb_index) - sc->start_index;
        return bar4_base_ + slot * sc->size;
    }

    // Size classes are laid out contiguously in BAR0; sum the sizes of all classes
    // ordered before this one to get the offset where this class's region begins.
    uint64_t region_offset = 0;
    for (const auto& earlier : size_classes_) {
        if (&earlier == sc) {
            break;
        }
        region_offset += earlier.count * earlier.size;
    }

    return bar0_base_ + region_offset + (static_cast<uint64_t>(tlb_index) - sc->start_index) * sc->size;
}

uint64_t SimulationTlbAllocator::get_tlb_reg_address_from_index(int tlb_index) {
    if (!find_size_class_for_index(tlb_index)) {
        UMD_THROW(error::RuntimeError, fmt::format("Invalid simulation TLB index {}.", tlb_index));
    }
    // TLB configuration registers start at this offset from BAR0 base.
    static constexpr uint64_t TLB_CONFIG_REG_BASE_OFFSET = 0x1fc00000;
    return bar0_base_ + TLB_CONFIG_REG_BASE_OFFSET + tlb_index * tlb_reg_size_bytes_;
}

const architecture_implementation* SimulationTlbAllocator::get_architecture_impl() const { return arch_impl_; }

tt::ARCH SimulationTlbAllocator::get_architecture() const { return architecture_; }

SimulationTlbAllocator::TlbSizeClass* SimulationTlbAllocator::find_size_class_for_index(int tlb_index) {
    // Returning nullptr for negative indices avoids signed/unsigned comparison
    // pitfalls below (where size_t promotion would turn -1 into SIZE_MAX).
    if (tlb_index < 0) {
        return nullptr;
    }
    auto idx = static_cast<size_t>(tlb_index);
    for (auto& sc : size_classes_) {
        if (sc.count > 0 && idx >= sc.start_index && idx < sc.start_index + sc.count) {
            return &sc;
        }
    }
    return nullptr;
}

void SimulationTlbAllocator::initialize_architecture_config() {
    architecture_ = arch_impl_->get_architecture();

    if (architecture_ == tt::ARCH::WORMHOLE_B0) {
        tlb_reg_size_bytes_ = 8;  // wormhole::TLB_CFG_REG_SIZE_BYTES.

        const auto& tlb_sizes = arch_impl_->get_tlb_sizes();
        size_classes_[ONE_MB].size = tlb_sizes[0];  // 1MB.
        size_classes_[ONE_MB].count = 156;
        size_classes_[TWO_MB].size = tlb_sizes[1];  // 2MB.
        size_classes_[TWO_MB].count = 10;
        size_classes_[SIXTEEN_MB].size = tlb_sizes[2];  // 16MB.
        size_classes_[SIXTEEN_MB].count = 20;
        // FOUR_GB stays at default (count=0).

    } else if (architecture_ == tt::ARCH::BLACKHOLE) {
        tlb_reg_size_bytes_ = 12;  // blackhole::TLB_CFG_REG_SIZE_BYTES.

        const auto& tlb_sizes = arch_impl_->get_tlb_sizes();
        size_classes_[TWO_MB].size = tlb_sizes[0];  // 2MB.
        size_classes_[TWO_MB].count = 202;
        size_classes_[FOUR_GB].size = tlb_sizes[1];  // 4GB.
        size_classes_[FOUR_GB].count = 8;
        // ONE_MB and SIXTEEN_MB stay at default (count=0).

    } else {
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

    // Lay out size classes contiguously in BAR0: each non-empty class starts where
    // the previous one ends.
    size_t next_start = 0;
    for (auto& sc : size_classes_) {
        sc.start_index = next_start;
        if (sc.count > 0) {
            sc.allocated.resize(sc.count, false);
            next_start += sc.count;
        }
    }
}

}  // namespace tt::umd
