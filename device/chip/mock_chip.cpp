/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/mock_chip.h"

namespace tt::umd {

MockChip::MockChip(tt_SocDescriptor soc_descriptor) : Chip(soc_descriptor) {}

bool MockChip::is_mmio_capable() const { return false; }

void MockChip::start_device() {}

void MockChip::close_device() {}

void MockChip::write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) {}

void MockChip::read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) {}

int MockChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    return 0;
}

void MockChip::l1_membar(const std::unordered_set<tt::umd::CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<tt::umd::CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<uint32_t>& channels) {}

void MockChip::deassert_risc_resets() {}

void MockChip::set_power_state(tt_DevicePowerState state) {}

int MockChip::get_clock() { return 0; }

SysmemManager* MockChip::get_sysmem_manager() {
    throw std::runtime_error(
        "MockChip::get_sysmem_manager is not available for this chip, it is only available for LocalChips.");
}

TLBManager* MockChip::get_tlb_manager() {
    throw std::runtime_error(
        "MockChip::get_tlb_manager is not available for this chip, it is only available for LocalChips.");
}

int MockChip::get_host_channel_size(std::uint32_t channel) {
    throw std::runtime_error("There are no host channels available.");
}

void MockChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    throw std::runtime_error("MockChip::write_to_sysmem is not available for this chip.");
}

void MockChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    throw std::runtime_error("MockChip::read_from_sysmem is not available for this chip.");
}

void MockChip::dma_write_to_device(const void* src, size_t size, tt_xy_pair core, uint64_t addr) {
    throw std::runtime_error("MockChip::dma_write_to_device is not available for this chip.");
}

void MockChip::dma_read_from_device(void* dst, size_t size, tt_xy_pair core, uint64_t addr) {
    throw std::runtime_error("MockChip::dma_read_from_device is not available for this chip.");
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> MockChip::get_fast_pcie_static_tlb_write_callable() {
    throw std::runtime_error("MockChip::get_fast_pcie_static_tlb_write_callable is not available for this chip.");
}

void MockChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {
    throw std::runtime_error("MockChip::set_remote_transfer_ethernet_cores is not available for this chip.");
}

void MockChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channel) {
    throw std::runtime_error("MockChip::set_remote_transfer_ethernet_cores is not available for this chip.");
}

int MockChip::get_numa_node() { throw std::runtime_error("MockChip::get_numa_node is not available for this chip."); }

void MockChip::wait_for_non_mmio_flush() {
    throw std::runtime_error("MockChip::wait_for_non_mmio_flush is not available for this chip.");
}

}  // namespace tt::umd
