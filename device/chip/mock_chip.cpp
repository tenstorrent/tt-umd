// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/mock_chip.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tt::umd {

static_assert(!std::is_abstract<MockChip>(), "MockChip must be non-abstract.");

MockChip::MockChip(const SocDescriptor& soc_descriptor) : Chip(soc_descriptor.arch), soc_descriptor_(soc_descriptor) {}

bool MockChip::is_mmio_capable() const { return false; }

void MockChip::start_device() {}

void MockChip::close_device() {}

TTDevice* MockChip::get_tt_device() { return nullptr; }

SysmemManager* MockChip::get_sysmem_manager() { return nullptr; }

TLBManager* MockChip::get_tlb_manager() { return nullptr; }

const SocDescriptor& MockChip::get_soc_descriptor() const { return soc_descriptor_; }

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

void MockChip::dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {}

void MockChip::noc_multicast_write(void* dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {}

int MockChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    // This designates success for the ARC enable eth queue message.
    *return_3 = 1;
    return 0;
}

void MockChip::wait_for_non_mmio_flush() {}

void MockChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<uint32_t>& channels) {}

void MockChip::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) {}

void MockChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {}

void MockChip::deassert_risc_resets() {}

void MockChip::set_power_state(DevicePowerState state) {}

int MockChip::get_clock() { return 0; }

int MockChip::get_numa_node() { return 0; }

void MockChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void MockChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {}
}  // namespace tt::umd
