// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/simulation_tlb_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip_helpers/tt_sim_tlb_manager.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"

namespace tt::umd {

// Forward declaration to avoid circular dependency.
class TTSimTlbManager;

TTSimTlbHandle::TTSimTlbHandle(TTSimTlbManager* manager, int tlb_id, size_t size, const TlbMapping tlb_mapping) :
    sim_manager_(manager), sim_tlb_id_(tlb_id), sim_size_(size), sim_mapping_(tlb_mapping) {
    // Compute the address for this TLB based on BAR0 base + TLB offset.
    if (sim_manager_) {
        sim_address_ = sim_manager_->get_tlb_address_from_index(sim_tlb_id_);
        tlb_reg_addr_ = sim_manager_->get_tlb_reg_address_from_index(sim_tlb_id_);
    }

    log_debug(LogUMD, "Created TTSimTlbHandle with ID {} size {} address 0x{:x}", sim_tlb_id_, sim_size_, sim_address_);
}

std::unique_ptr<TTSimTlbHandle> TTSimTlbHandle::create(
    TTSimTlbManager* manager, int tlb_id, size_t size, const TlbMapping tlb_mapping) {
    // We need to bypass the normal constructor to avoid hardware operations
    // Use the private constructor through make_unique won't work, so we use new.
    auto* handle = new TTSimTlbHandle(manager, tlb_id, size, tlb_mapping);
    return std::unique_ptr<TTSimTlbHandle>(handle);
}

TTSimTlbHandle::~TTSimTlbHandle() noexcept { free_tlb(); }

void TTSimTlbHandle::configure(const tlb_data& new_config) {
    sim_config_ = new_config;

    // Get architecture from manager to determine correct offsets.
    const architecture_implementation* arch_impl = sim_manager_->get_architecture_impl();
    tt::ARCH architecture = arch_impl->get_architecture();

    // Determine which TLB offset structure to use based on architecture and size.
    const tlb_offsets* offsets = nullptr;
    constexpr size_t SIZE_1MB = 1024 * 1024;
    constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    constexpr size_t SIZE_4GB = 4ULL * 1024ULL * 1024ULL * 1024ULL;

    if (architecture == tt::ARCH::WORMHOLE_B0) {
        if (sim_size_ == SIZE_1MB) {
            offsets = &wormhole::TLB_1M_OFFSET;
        } else if (sim_size_ == SIZE_2MB) {
            offsets = &wormhole::TLB_2M_OFFSET;
        } else if (sim_size_ == SIZE_16MB) {
            offsets = &wormhole::TLB_16M_OFFSET;
        } else {
            log_warning(LogUMD, "Unknown Wormhole TLB size: {} bytes, defaulting to 1MB offsets", sim_size_);
            offsets = &wormhole::TLB_1M_OFFSET;
        }
    } else if (architecture == tt::ARCH::BLACKHOLE) {
        if (sim_size_ == SIZE_2MB) {
            offsets = &blackhole::TLB_2M_OFFSET;
        } else if (sim_size_ == SIZE_4GB) {
            offsets = &blackhole::TLB_4G_OFFSET;
        } else {
            log_warning(LogUMD, "Unknown Blackhole TLB size: {} bytes, defaulting to 2MB offsets", sim_size_);
            offsets = &blackhole::TLB_2M_OFFSET;
        }
    } else {
        log_warning(LogUMD, "Unsupported architecture for TLB configuration: {}", static_cast<int>(architecture));
        // Default to Wormhole 1MB for unknown architectures.
        offsets = &wormhole::TLB_1M_OFFSET;
    }

    // Apply the offsets to create the register value.
    auto [reg_val, reg_val_high] = new_config.apply_offset(*offsets);

    // Get the communicator to write the register.
    TTSimCommunicator* communicator = sim_manager_->get_communicator();

    // Write the TLB register based on architecture.
    if (architecture == tt::ARCH::BLACKHOLE) {
        // Blackhole uses 12 bytes (96 bits) - 8 bytes low + 4 bytes from high.
        uint8_t reg_data[12];

        // First 8 bytes: low part.
        std::memcpy(reg_data, &reg_val, 8);

        // Next 4 bytes: lowest 4 bytes of high part.
        uint32_t reg_val_high_low = static_cast<uint32_t>(reg_val_high & 0xFFFFFFFF);
        std::memcpy(reg_data + 8, &reg_val_high_low, 4);

        communicator->pci_mem_write_bytes(tlb_reg_addr_, reg_data, 12);
    } else {
        // Wormhole uses 8 bytes (64 bits).
        communicator->pci_mem_write_bytes(tlb_reg_addr_, &reg_val, 8);
    }

    log_debug(
        LogUMD,
        "Configured simulation TLB {} ({}) address 0x{:x} reg_addr 0x{:x} ({} bytes) with local_offset: {}, x_end: {}, "
        "y_end: {}, ordering: {}",
        sim_tlb_id_,
        architecture == tt::ARCH::WORMHOLE_B0 ? "Wormhole" : "Blackhole",
        sim_address_,
        tlb_reg_addr_,
        architecture == tt::ARCH::BLACKHOLE ? 12 : 8,
        new_config.local_offset,
        new_config.x_end,
        new_config.y_end,
        new_config.ordering);
}

uint8_t* TTSimTlbHandle::get_base() {
    // For simulation, return the computed address as a pointer
    // This allows code expecting a memory pointer to work with simulated addresses.
    return reinterpret_cast<uint8_t*>(sim_address_);
}

size_t TTSimTlbHandle::get_size() const { return sim_size_; }

const tlb_data& TTSimTlbHandle::get_config() const { return sim_config_; }

TlbMapping TTSimTlbHandle::get_tlb_mapping() const { return sim_mapping_; }

int TTSimTlbHandle::get_tlb_id() const { return sim_tlb_id_; }

uint64_t TTSimTlbHandle::get_address() const { return sim_address_; }

void TTSimTlbHandle::free_tlb() noexcept {
    if (sim_manager_) {
        sim_manager_->deallocate_tlb_index(sim_tlb_id_);
        sim_manager_ = nullptr;

        log_debug(LogUMD, "Freed simulation TLB with ID {}", sim_tlb_id_);
    }
}

}  // namespace tt::umd
