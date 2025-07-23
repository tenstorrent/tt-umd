/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/remote_chip.h"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/chip/local_chip.h"
#include "umd/device/wormhole_implementation.h"

namespace tt::umd {

static_assert(!std::is_abstract<RemoteChip>(), "RemoteChip must be non-abstract.");

RemoteChip::RemoteChip(tt_SocDescriptor soc_descriptor, std::unique_ptr<RemoteWormholeTTDevice> remote_tt_device) :
    Chip(soc_descriptor) {
    local_chip_ = remote_tt_device->get_local_chip();
    remote_communication_ = remote_tt_device->get_remote_communication();
    tt_device_ = std::move(remote_tt_device);
    chip_info_ = tt_device_->get_chip_info();
    TT_ASSERT(soc_descriptor_.arch != tt::ARCH::BLACKHOLE, "Non-MMIO targets not supported in Blackhole");
    wait_chip_to_be_ready();
}

bool RemoteChip::is_mmio_capable() const { return false; }

void RemoteChip::start_device() {}

void RemoteChip::close_device() {
    // Investigating https://github.com/tenstorrent/tt-metal/issues/25377 found that closing device that was already put
    // in LONG_IDLE by tt-smi reset would hang
    if ((uint32_t)local_chip_->get_clock() != local_chip_->get_tt_device()->get_min_clock_freq()) {
        if ((uint32_t)get_clock() != get_tt_device()->get_min_clock_freq()) {
            set_power_state(tt_DevicePowerState::LONG_IDLE);
            send_tensix_risc_reset(TENSIX_ASSERT_SOFT_RESET);
        }
    }
}

void RemoteChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    tt_device_->write_to_device(src, translate_chip_coord_to_translated(core), l1_dest, size);
}

void RemoteChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    tt_device_->read_from_device(dest, translate_chip_coord_to_translated(core), l1_src, size);
}

void RemoteChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void RemoteChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

void RemoteChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    throw std::runtime_error("RemoteChip::dma_write_to_device is not available for this chip.");
}

void RemoteChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    throw std::runtime_error("RemoteChip::dma_read_from_device is not available for this chip.");
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> RemoteChip::get_fast_pcie_static_tlb_write_callable() {
    throw std::runtime_error("RemoteChip::get_fast_pcie_static_tlb_write_callable is not available for this chip.");
}

void RemoteChip::wait_for_non_mmio_flush() {
    TT_ASSERT(soc_descriptor_.arch != tt::ARCH::BLACKHOLE, "Non-MMIO flush not supported in Blackhole");
    remote_communication_->wait_for_non_mmio_flush();
}

void RemoteChip::l1_membar(const std::unordered_set<CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<uint32_t>& channels) { wait_for_non_mmio_flush(); }

void RemoteChip::deassert_risc_resets() { local_chip_->deassert_risc_resets(); }

void RemoteChip::set_power_state(tt_DevicePowerState state) {
    if (soc_descriptor_.arch == tt::ARCH::WORMHOLE_B0) {
        uint32_t msg = get_power_state_arc_msg(state);
        int exit_code = arc_msg(wormhole::ARC_MSG_COMMON_PREFIX | msg, true, 0, 0);
        TT_ASSERT(exit_code == 0, "Failed to set power state to {} with exit code: {}", (int)state, exit_code);
    } else if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
        throw std::runtime_error("set_power_state not supported for remote chips on Blackhole.");
    }
    wait_for_aiclk_value(tt_device_.get(), state);
}

int RemoteChip::get_clock() { return tt_device_->get_clock(); }

int RemoteChip::get_num_host_channels() { return 0; }

int RemoteChip::get_host_channel_size(std::uint32_t channel) {
    throw std::runtime_error("There are no host channels available.");
}

void RemoteChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    throw std::runtime_error("RemoteChip::write_to_sysmem is not available for this chip.");
}

void RemoteChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    throw std::runtime_error("RemoteChip::read_from_sysmem is not available for this chip.");
}

int RemoteChip::get_numa_node() {
    throw std::runtime_error("RemoteChip::get_numa_node is not available for this chip.");
}

void RemoteChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void RemoteChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channel) {}

TTDevice* RemoteChip::get_tt_device() { return tt_device_.get(); }

SysmemManager* RemoteChip::get_sysmem_manager() {
    throw std::runtime_error("RemoteChip::get_sysmem_manager is not available for this chip.");
}

TLBManager* RemoteChip::get_tlb_manager() {
    throw std::runtime_error("RemoteChip::get_tlb_manager is not available for this chip.");
}

}  // namespace tt::umd
