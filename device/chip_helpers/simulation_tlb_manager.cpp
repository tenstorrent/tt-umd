// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_tlb_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "assert.hpp"
#include "tracy.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

SimulationTlbManager::SimulationTlbManager(
    TTDevice* tt_device, uint64_t bar0_base, const architecture_implementation* arch_impl, TlbWindowFactory factory) :
    TLBManager(tt_device), bar0_base_(bar0_base), arch_impl_(arch_impl), factory_(std::move(factory)) {
    // Initialize architecture-specific configuration.
    initialize_architecture_config();
}

int SimulationTlbManager::allocate_tlb_index(size_t size) {
    ZoneScopedC(tracy::Color::Cyan);
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

void SimulationTlbManager::deallocate_tlb_index(int tlb_index) {
    ZoneScopedC(tracy::Color::Cyan);
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

size_t SimulationTlbManager::get_tlb_size_from_index(int tlb_index) {
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

uint64_t SimulationTlbManager::get_tlb_address_from_index(int tlb_index) {
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
            UMD_THROW(
                error::RuntimeError, "4GB TLBs in Blackhole are not currently supported in simulation TLB allocation.");
        }
    }

    // Invalid TLB index.
    return 0;
}

std::unique_ptr<TlbWindow> SimulationTlbManager::allocate_tlb_window(
    tlb_data config, const TlbMapping mapping, const size_t tlb_size) {
    ZoneScopedC(tracy::Color::Cyan);

    int tlb_index = allocate_tlb_index(tlb_size);
    if (tlb_index == -1) {
        UMD_THROW(error::RuntimeError, "No available TLB of requested size.");
    }

    size_t actual_tlb_size = get_tlb_size_from_index(tlb_index);

    return factory_(this, tlb_index, actual_tlb_size, mapping, config);
}

uint64_t SimulationTlbManager::get_tlb_reg_address_from_index(int tlb_index) {
    // TLB configuration registers start at this offset from BAR0 base.
    static constexpr uint64_t TLB_CONFIG_REG_BASE_OFFSET = 0x1fc00000;
    return bar0_base_ + TLB_CONFIG_REG_BASE_OFFSET + tlb_index * tlb_reg_size_bytes_;
}

const architecture_implementation* SimulationTlbManager::get_architecture_impl() const { return arch_impl_; }

std::unique_ptr<TlbWindow> SimulationTlbManager::allocate_default_tlb_window() {
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    // Quasar has no real TLBs; the communicator handles all I/O underneath.
    // The size here is a dummy value — it just needs to be large enough so that
    // TlbWindow::validate doesn't reject any valid access (size 0 would cause
    // division by zero in RtlSimTlbHandle::configure).
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    switch (architecture_) {
        case tt::ARCH::BLACKHOLE:
            return allocate_tlb_window({}, TlbMapping::WC, SIZE_2MB);
        case tt::ARCH::WORMHOLE_B0:
            return allocate_tlb_window({}, TlbMapping::WC, SIZE_16MB);
        case tt::ARCH::QUASAR:
            return factory_(this, 0, SIZE_4GB, TlbMapping::WC, {});
        default:
            log_debug(
                LogUMD,
                "Architecture {} does not support TLB allocation, returning nullptr.",
                tt::arch_to_str(architecture_));
            return nullptr;
    }
}

void SimulationTlbManager::initialize_architecture_config() {
    architecture_ = get_architecture_impl()->get_architecture();

    if (architecture_ == tt::ARCH::WORMHOLE_B0) {
        // Wormhole B0 configuration.
        tlb_reg_size_bytes_ = 8;  // wormhole::TLB_CFG_REG_SIZE_BYTES

        const auto* arch_impl = get_architecture_impl();
        const auto& tlb_sizes = arch_impl->get_tlb_sizes();
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
        // tlb_4gb_allocated_ remains empty

    } else if (architecture_ == tt::ARCH::BLACKHOLE) {
        // Blackhole configuration.
        tlb_reg_size_bytes_ = 12;  // blackhole::TLB_CFG_REG_SIZE_BYTES

        const auto* arch_impl = get_architecture_impl();
        const auto& tlb_sizes = arch_impl->get_tlb_sizes();
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
        // tlb_1mb_allocated_ and tlb_16mb_allocated_ remain empty
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
