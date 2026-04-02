// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/tlb_allocator.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"

namespace tt::umd {

TlbAllocator::TlbAllocator(uint64_t bar0_base, const architecture_implementation* arch_impl) :
    bar0_base_(bar0_base), arch_impl_(arch_impl) {
    initialize_architecture_config();
}

int TlbAllocator::allocate_tlb_index(size_t size) {
    std::lock_guard<std::mutex> lock(allocation_mutex_);

    if (size == 0) {
        // Allocate any available TLB, prefer smaller sizes first.
        if (tlb_1mb_size_ > 0) {
            int tlb_index = allocate_tlb_index(tlb_1mb_size_);
            if (tlb_index != -1) {
                return tlb_index;
            }
        }

        if (tlb_2mb_size_ > 0) {
            int tlb_index = allocate_tlb_index(tlb_2mb_size_);
            if (tlb_index != -1) {
                return tlb_index;
            }
        }

        if (tlb_16mb_size_ > 0) {
            int tlb_index = allocate_tlb_index(tlb_16mb_size_);
            if (tlb_index != -1) {
                return tlb_index;
            }
        }

        if (tlb_4gb_size_ > 0) {
            int tlb_index = allocate_tlb_index(tlb_4gb_size_);
            return tlb_index;
        }

        return -1;
    }

    // Try 1MB TLBs (Wormhole only).
    if (tlb_1mb_size_ > 0 && size <= tlb_1mb_size_) {
        for (size_t i = 0; i < tlb_1mb_count_; ++i) {
            if (!tlb_1mb_allocated_[i]) {
                tlb_1mb_allocated_[i] = true;
                return static_cast<int>(tlb_1mb_start_index_ + i);
            }
        }
    }

    // Try 2MB TLBs (both architectures).
    if (tlb_2mb_size_ > 0 && size <= tlb_2mb_size_) {
        for (size_t i = 0; i < tlb_2mb_count_; ++i) {
            if (!tlb_2mb_allocated_[i]) {
                tlb_2mb_allocated_[i] = true;
                return static_cast<int>(tlb_2mb_start_index_ + i);
            }
        }
    }

    // Try 16MB TLBs (Wormhole only).
    if (tlb_16mb_size_ > 0 && size <= tlb_16mb_size_) {
        for (size_t i = 0; i < tlb_16mb_count_; ++i) {
            if (!tlb_16mb_allocated_[i]) {
                tlb_16mb_allocated_[i] = true;
                return static_cast<int>(tlb_16mb_start_index_ + i);
            }
        }
    }

    // Try 4GB TLBs (Blackhole only).
    if (tlb_4gb_size_ > 0 && size <= tlb_4gb_size_) {
        for (size_t i = 0; i < tlb_4gb_count_; ++i) {
            if (!tlb_4gb_allocated_[i]) {
                tlb_4gb_allocated_[i] = true;
                return static_cast<int>(tlb_4gb_start_index_ + i);
            }
        }
    }

    return -1;  // No available TLB
}

void TlbAllocator::deallocate_tlb_index(int tlb_index) {
    std::lock_guard<std::mutex> lock(allocation_mutex_);

    // Check 1MB TLBs (Wormhole only).
    if (tlb_1mb_count_ > 0 && tlb_index >= tlb_1mb_start_index_ && tlb_index < tlb_2mb_start_index_) {
        size_t local_index = tlb_index - tlb_1mb_start_index_;
        if (local_index < tlb_1mb_count_) {
            tlb_1mb_allocated_[local_index] = false;
        }
    }
    // Check 2MB TLBs (both architectures).
    else if (
        tlb_2mb_count_ > 0 && tlb_index >= tlb_2mb_start_index_ &&
        (tlb_16mb_count_ == 0 || tlb_index < tlb_16mb_start_index_) &&
        (tlb_4gb_count_ == 0 || tlb_index < tlb_4gb_start_index_)) {
        size_t local_index = tlb_index - tlb_2mb_start_index_;
        if (local_index < tlb_2mb_count_) {
            tlb_2mb_allocated_[local_index] = false;
        }
    }
    // Check 16MB TLBs (Wormhole only).
    else if (
        tlb_16mb_count_ > 0 && tlb_index >= tlb_16mb_start_index_ &&
        (tlb_4gb_count_ == 0 || tlb_index < tlb_4gb_start_index_)) {
        size_t local_index = tlb_index - tlb_16mb_start_index_;
        if (local_index < tlb_16mb_count_) {
            tlb_16mb_allocated_[local_index] = false;
        }
    }
    // Check 4GB TLBs (Blackhole only).
    else if (tlb_4gb_count_ > 0 && tlb_index >= tlb_4gb_start_index_) {
        size_t local_index = tlb_index - tlb_4gb_start_index_;
        if (local_index < tlb_4gb_count_) {
            tlb_4gb_allocated_[local_index] = false;
        }
    }
}

size_t TlbAllocator::get_tlb_size_from_index(int tlb_index) const {
    // Check 1MB TLBs (Wormhole only).
    if (tlb_1mb_count_ > 0 && tlb_index >= tlb_1mb_start_index_ && tlb_index < tlb_2mb_start_index_) {
        return tlb_1mb_size_;
    }
    // Check 2MB TLBs (both architectures).
    else if (
        tlb_2mb_count_ > 0 && tlb_index >= tlb_2mb_start_index_ &&
        (tlb_16mb_count_ == 0 || tlb_index < tlb_16mb_start_index_) &&
        (tlb_4gb_count_ == 0 || tlb_index < tlb_4gb_start_index_)) {
        return tlb_2mb_size_;
    }
    // Check 16MB TLBs (Wormhole only).
    else if (
        tlb_16mb_count_ > 0 && tlb_index >= tlb_16mb_start_index_ &&
        (tlb_4gb_count_ == 0 || tlb_index < tlb_4gb_start_index_)) {
        return tlb_16mb_size_;
    }
    // Check 4GB TLBs (Blackhole only).
    else if (tlb_4gb_count_ > 0 && tlb_index >= tlb_4gb_start_index_) {
        return tlb_4gb_size_;
    }
    return 0;
}

uint64_t TlbAllocator::get_tlb_address_from_index(int tlb_index) const {
    if (architecture_ == tt::ARCH::WORMHOLE_B0) {
        // Wormhole B0 address calculation.
        if (tlb_1mb_count_ > 0 && tlb_index >= tlb_1mb_start_index_ && tlb_index < tlb_2mb_start_index_) {
            // 1MB TLBs: indices 0-155
            return bar0_base_ + (static_cast<uint64_t>(tlb_index) * tlb_1mb_size_);
        } else if (tlb_2mb_count_ > 0 && tlb_index >= tlb_2mb_start_index_ && tlb_index < tlb_16mb_start_index_) {
            // 2MB TLBs: indices 156-165
            uint64_t offset_1mb_region = tlb_1mb_count_ * tlb_1mb_size_;
            uint64_t offset_in_2mb_region = static_cast<uint64_t>(tlb_index - tlb_2mb_start_index_) * tlb_2mb_size_;
            return bar0_base_ + offset_1mb_region + offset_in_2mb_region;
        } else if (tlb_16mb_count_ > 0 && tlb_index >= tlb_16mb_start_index_) {
            // 16MB TLBs: indices 166-185
            uint64_t offset_1mb_region = tlb_1mb_count_ * tlb_1mb_size_;
            uint64_t offset_2mb_region = tlb_2mb_count_ * tlb_2mb_size_;
            uint64_t offset_in_16mb_region = static_cast<uint64_t>(tlb_index - tlb_16mb_start_index_) * tlb_16mb_size_;
            return bar0_base_ + offset_1mb_region + offset_2mb_region + offset_in_16mb_region;
        }
    } else if (architecture_ == tt::ARCH::BLACKHOLE) {
        // Blackhole address calculation.
        if (tlb_2mb_count_ > 0 && tlb_index >= tlb_2mb_start_index_ && tlb_index < tlb_4gb_start_index_) {
            // 2MB TLBs: indices 0-201
            return bar0_base_ + (static_cast<uint64_t>(tlb_index) * tlb_2mb_size_);
        } else if (tlb_4gb_count_ > 0 && tlb_index >= tlb_4gb_start_index_) {
            // 4GB TLBs are in BAR4, not BAR0 for Blackhole
            throw std::runtime_error("4GB TLBs in Blackhole are not currently supported in simulation TLB allocation.");
        }
    }

    // Invalid TLB index.
    return 0;
}

uint64_t TlbAllocator::get_tlb_reg_address_from_index(int tlb_index) const {
    // TLB configuration registers start at this offset from BAR0 base.
    static constexpr uint64_t TLB_CONFIG_REG_BASE_OFFSET = 0x1fc00000;
    return bar0_base_ + TLB_CONFIG_REG_BASE_OFFSET + tlb_index * tlb_reg_size_bytes_;
}

const architecture_implementation* TlbAllocator::get_architecture_impl() const { return arch_impl_; }

tt::ARCH TlbAllocator::get_architecture() const { return architecture_; }

size_t TlbAllocator::get_default_tlb_size() const {
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    switch (architecture_) {
        case tt::ARCH::BLACKHOLE:
            return SIZE_2MB;
        case tt::ARCH::WORMHOLE_B0:
            return SIZE_16MB;
        default:
            return 0;
    }
}

void TlbAllocator::initialize_architecture_config() {
    architecture_ = arch_impl_->get_architecture();

    if (architecture_ == tt::ARCH::WORMHOLE_B0) {
        // Wormhole B0 configuration.
        tlb_reg_size_bytes_ = 8;  // wormhole::TLB_CFG_REG_SIZE_BYTES

        const auto& tlb_sizes = arch_impl_->get_tlb_sizes();
        tlb_1mb_size_ = tlb_sizes[0];   // 1MB
        tlb_2mb_size_ = tlb_sizes[1];   // 2MB
        tlb_16mb_size_ = tlb_sizes[2];  // 16MB
        tlb_4gb_size_ = 0;              // Not supported

        tlb_1mb_count_ = 156;
        tlb_2mb_count_ = 10;
        tlb_16mb_count_ = 20;
        tlb_4gb_count_ = 0;

        tlb_1mb_start_index_ = 0;
        tlb_2mb_start_index_ = tlb_1mb_start_index_ + tlb_1mb_count_;
        tlb_16mb_start_index_ = tlb_2mb_start_index_ + tlb_2mb_count_;
        tlb_4gb_start_index_ = 0;  // Not applicable

        // Initialize allocation tracking.
        tlb_1mb_allocated_.resize(tlb_1mb_count_, false);
        tlb_2mb_allocated_.resize(tlb_2mb_count_, false);
        tlb_16mb_allocated_.resize(tlb_16mb_count_, false);

    } else if (architecture_ == tt::ARCH::BLACKHOLE) {
        // Blackhole configuration.
        tlb_reg_size_bytes_ = 12;  // blackhole::TLB_CFG_REG_SIZE_BYTES

        const auto& tlb_sizes = arch_impl_->get_tlb_sizes();
        tlb_1mb_size_ = 0;             // Not supported
        tlb_2mb_size_ = tlb_sizes[0];  // 2MB
        tlb_16mb_size_ = 0;            // Not supported
        tlb_4gb_size_ = tlb_sizes[1];  // 4GB (second element in Blackhole tlb_sizes)

        tlb_1mb_count_ = 0;
        tlb_2mb_count_ = 202;
        tlb_16mb_count_ = 0;
        tlb_4gb_count_ = 8;

        tlb_1mb_start_index_ = 0;  // Not applicable
        tlb_2mb_start_index_ = 0;
        tlb_16mb_start_index_ = 0;  // Not applicable
        tlb_4gb_start_index_ = tlb_2mb_count_;

        // Initialize allocation tracking.
        tlb_2mb_allocated_.resize(tlb_2mb_count_, false);
        tlb_4gb_allocated_.resize(tlb_4gb_count_, false);

    } else {
        log_debug(
            LogUMD,
            fmt::format(
                "Architecture {} does not yet have support for TLB management in simulation. UMD will use legacy "
                "tile_wr_bytes and tile_rd_bytes path.",
                tt::arch_to_str(architecture_)));
    }
}

}  // namespace tt::umd
