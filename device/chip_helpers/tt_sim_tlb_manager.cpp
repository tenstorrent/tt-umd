// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/tt_sim_tlb_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "assert.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/pcie/simulation_tlb_handle.hpp"
#include "umd/device/pcie/simulation_tlb_window.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

TTSimTlbManager::TTSimTlbManager(TTDevice* tt_device) : TLBManager(tt_device) {
    tt_sim_tt_device_ = dynamic_cast<TTSimTTDevice*>(tt_device);
    bar0_base_ = tt_sim_tt_device_->bar0_base;
    tlb_registers_base_ = tt_sim_tt_device_->tlb_registers_base;

    // Initialize architecture-specific configuration.
    initialize_architecture_config();
}

int TTSimTlbManager::allocate_tlb_index(size_t size) {
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

void TTSimTlbManager::deallocate_tlb_index(int tlb_index) {
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

size_t TTSimTlbManager::get_tlb_size_from_index(int tlb_index) {
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

uint64_t TTSimTlbManager::get_tlb_address_from_index(int tlb_index) {
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
            // For now, use BAR0 base but this might need adjustment based on actual BAR4 mapping.
            uint64_t offset_2mb_region = tlb_2mb_count_ * tlb_2mb_size_;
            uint64_t offset_in_4gb_region = static_cast<uint64_t>(tlb_index - tlb_4gb_start_index_) * tlb_4gb_size_;
            return bar0_base_ + offset_2mb_region + offset_in_4gb_region;
        }
    }

    // Invalid TLB index.
    return 0;
}

std::unique_ptr<TlbWindow> TTSimTlbManager::allocate_tlb_window(
    tlb_data config, const TlbMapping mapping, const size_t tlb_size) {
    int tlb_index = allocate_tlb_index(tlb_size);
    if (tlb_index == -1) {
        throw std::runtime_error("No available TLB of requested size");
    }

    size_t actual_tlb_size = get_tlb_size_from_index(tlb_index);

    auto tlb_handle = TTSimTlbHandle::create(this, tlb_index, actual_tlb_size, mapping);
    return std::make_unique<SimulationTlbWindow>(std::move(tlb_handle), get_communicator(), config);
}

uint64_t TTSimTlbManager::get_tlb_reg_address_from_index(int tlb_index) {
    return bar0_base_ + 0x1fc00000 + tlb_index * tlb_reg_size_bytes_;
}

const architecture_implementation* TTSimTlbManager::get_architecture_impl() const {
    return tt_sim_tt_device_->get_architecture_impl();
}

TTSimCommunicator* TTSimTlbManager::get_communicator() const { return tt_sim_tt_device_->get_communicator(); }

void TTSimTlbManager::initialize_architecture_config() {
    architecture_ = tt_sim_tt_device_->get_architecture_impl()->get_architecture();

    if (architecture_ == tt::ARCH::WORMHOLE_B0) {
        // Wormhole B0 configuration.
        tlb_reg_size_bytes_ = 8;  // wormhole::TLB_CFG_REG_SIZE_BYTES

        tlb_1mb_size_ = 1024 * 1024;        // 1MB
        tlb_2mb_size_ = 2 * 1024 * 1024;    // 2MB
        tlb_16mb_size_ = 16 * 1024 * 1024;  // 16MB
        tlb_4gb_size_ = 0;                  // Not supported

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

        tlb_1mb_size_ = 0;                                   // Not supported
        tlb_2mb_size_ = 2 * 1024 * 1024;                     // 2MB
        tlb_16mb_size_ = 0;                                  // Not supported
        tlb_4gb_size_ = 4ULL * 1024ULL * 1024ULL * 1024ULL;  // 4GB

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
        throw std::runtime_error(
            "Unsupported architecture for TTSim TLB Manager: " + std::to_string(static_cast<int>(architecture_)));
    }
}

}  // namespace tt::umd
