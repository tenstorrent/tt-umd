// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/tt_sim_tlb_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip_helpers/simulation_tlb_manager.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"

namespace tt::umd {

// Forward declaration to avoid circular dependency.
class SimulationTlbManager;

TTSimTlbHandle::TTSimTlbHandle(
    SimulationTlbManager* manager,
    TTSimCommunicator* communicator,
    int tlb_id,
    size_t size,
    const TlbMapping tlb_mapping) :
    sim_manager_(manager), sim_communicator_(communicator) {
    tlb_id_ = tlb_id;
    tlb_size_ = size;
    tlb_mapping_ = tlb_mapping;

    // Compute the address for this TLB based on BAR0 base + TLB offset.
    if (sim_manager_) {
        tlb_base_ = reinterpret_cast<uint8_t*>(sim_manager_->get_tlb_address_from_index(tlb_id_));
        tlb_reg_addr_ = sim_manager_->get_tlb_reg_address_from_index(tlb_id_);
    }

    log_debug(
        LogUMD,
        "Created TTSimTlbHandle with ID {} size {} address 0x{:x}",
        tlb_id_,
        tlb_size_,
        reinterpret_cast<uint64_t>(tlb_base_));
}

std::unique_ptr<TTSimTlbHandle> TTSimTlbHandle::create(
    SimulationTlbManager* manager,
    TTSimCommunicator* communicator,
    int tlb_id,
    size_t size,
    const TlbMapping tlb_mapping) {
    return std::unique_ptr<TTSimTlbHandle>(new TTSimTlbHandle(manager, communicator, tlb_id, size, tlb_mapping));
}

TTSimTlbHandle::~TTSimTlbHandle() noexcept { TTSimTlbHandle::free_tlb(); }

void TTSimTlbHandle::configure(const tlb_data& new_config) {
    tlb_config_ = new_config;
    tlb_config_.local_offset = new_config.local_offset / tlb_size_;

    // These fields are not supported for TTSim, so we set it to 0.
    tlb_config_.ordering = 0;
    tlb_config_.static_vc = 0;

    // Get architecture from manager to determine correct offsets.
    const architecture_implementation* arch_impl = sim_manager_->get_architecture_impl();
    tt::ARCH architecture = arch_impl->get_architecture();

    log_debug(
        LogUMD,
        "Configured simulation TLB {} ({}) address 0x{:x} reg_addr 0x{:x} ({} bytes) with local_offset: {}, x_end: {}, "
        "y_end: {}, ordering: {}",
        tlb_id_,
        architecture == tt::ARCH::WORMHOLE_B0 ? "Wormhole" : "Blackhole",
        reinterpret_cast<uint64_t>(tlb_base_),
        tlb_reg_addr_,
        architecture == tt::ARCH::BLACKHOLE ? 12 : 8,
        tlb_config_.local_offset,
        tlb_config_.x_end,
        tlb_config_.y_end,
        tlb_config_.ordering);

    // Determine which TLB offset structure to use based on architecture and size.
    const tlb_offsets* offsets = nullptr;
    constexpr size_t SIZE_1MB = 1024 * 1024;
    constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    constexpr size_t SIZE_4GB = 4ULL * 1024ULL * 1024ULL * 1024ULL;

    if (architecture == tt::ARCH::WORMHOLE_B0) {
        if (tlb_size_ == SIZE_1MB) {
            offsets = &wormhole::TLB_1M_OFFSET;
        } else if (tlb_size_ == SIZE_2MB) {
            offsets = &wormhole::TLB_2M_OFFSET;
        } else if (tlb_size_ == SIZE_16MB) {
            offsets = &wormhole::TLB_16M_OFFSET;
        } else {
            log_warning(LogUMD, "Unknown Wormhole TLB size: {} bytes, defaulting to 1MB offsets", tlb_size_);
            offsets = &wormhole::TLB_1M_OFFSET;
        }
    } else if (architecture == tt::ARCH::BLACKHOLE) {
        if (tlb_size_ == SIZE_2MB) {
            offsets = &blackhole::TLB_2M_OFFSET;
        } else if (tlb_size_ == SIZE_4GB) {
            offsets = &blackhole::TLB_4G_OFFSET;
        } else {
            log_warning(LogUMD, "Unknown Blackhole TLB size: {} bytes, defaulting to 2MB offsets", tlb_size_);
            offsets = &blackhole::TLB_2M_OFFSET;
        }
    } else {
        log_warning(LogUMD, "Unsupported architecture for TLB configuration: {}", static_cast<int>(architecture));
        // Default to Wormhole 1MB for unknown architectures.
        offsets = &wormhole::TLB_1M_OFFSET;
    }

    // Apply the offsets to create the register value.
    auto [reg_val, reg_val_high] = tlb_config_.apply_offset(*offsets);

    // Use the communicator to write the register.
    TTSimCommunicator* communicator = sim_communicator_;

    // Write the TLB register based on architecture.
    if (architecture == tt::ARCH::BLACKHOLE) {
        // Blackhole uses 12 bytes (96 bits) - 8 bytes low + 4 bytes from high.
        uint8_t reg_data[12];

        // First 8 bytes: low part.
        std::memcpy(reg_data, &reg_val, 8);

        // Next 4 bytes: lowest 4 bytes of high part.
        uint32_t reg_val_high_low = static_cast<uint32_t>(reg_val_high & 0xFFFFFFFF);
        std::memcpy(reg_data + 8, &reg_val_high_low, 4);

        communicator->pci_mem_write_bytes(tlb_reg_addr_, reg_data, 4);
        communicator->pci_mem_write_bytes(tlb_reg_addr_ + 4, reg_data + 4, 4);
        communicator->pci_mem_write_bytes(tlb_reg_addr_ + 8, reg_data + 8, 4);
    } else {
        // Wormhole uses 8 bytes (64 bits).
        communicator->pci_mem_write_bytes(tlb_reg_addr_, &reg_val, 8);
    }
}

void TTSimTlbHandle::free_tlb() noexcept {
    if (sim_manager_) {
        sim_manager_->deallocate_tlb_index(tlb_id_);
        sim_manager_ = nullptr;

        log_debug(LogUMD, "Freed simulation TLB with ID {}", tlb_id_);
    }
}

}  // namespace tt::umd
