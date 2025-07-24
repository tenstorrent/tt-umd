/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/mock_chip.h"

namespace tt::umd {

static_assert(!std::is_abstract<MockChip>(), "MockChip must be non-abstract.");

MockChip::MockChip(tt_SocDescriptor soc_descriptor) : Chip(soc_descriptor) {}

bool MockChip::is_mmio_capable() const { return false; }

void MockChip::start_device() {}

void MockChip::close_device() {}

TTDevice* MockChip::get_tt_device() { return nullptr; }

SysmemManager* MockChip::get_sysmem_manager() { return nullptr; }

TLBManager* MockChip::get_tlb_manager() { return nullptr; }

int MockChip::get_num_host_channels() { return 0; }

int MockChip::get_host_channel_size(std::uint32_t channel) { return 0; }

void MockChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {}

void MockChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {}

void MockChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {}

void MockChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {}

void MockChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {}

void MockChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {}

void MockChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {}

void MockChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {}

std::function<void(uint32_t, uint32_t, const uint8_t*)> MockChip::get_fast_pcie_static_tlb_write_callable() {
    return [](uint32_t, uint32_t, const uint8_t*) {
        // No-op for mock chip
    };
}

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

void MockChip::wait_for_non_mmio_flush() {}

void MockChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<uint32_t>& channels) {}

void MockChip::deassert_risc_resets() {}

void MockChip::set_power_state(tt_DevicePowerState state) {}

int MockChip::get_clock() { return 0; }

int MockChip::get_numa_node() { return 0; }

void MockChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void MockChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channel) {}
}  // namespace tt::umd
