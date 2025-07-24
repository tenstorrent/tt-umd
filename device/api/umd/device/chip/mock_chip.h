/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"

namespace tt::umd {
class MockChip : public Chip {
public:
    MockChip(tt_SocDescriptor soc_descriptor);
    bool is_mmio_capable() const override;

    void start_device() override;
    void close_device() override;

    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    int get_num_host_channels() override;
    int get_host_channel_size(std::uint32_t channel) override;
    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;
    void write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) override;
    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;

    std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable() override;

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done,
        uint32_t arg0,
        uint32_t arg1,
        uint32_t timeout_ms,
        uint32_t* return_3,
        uint32_t* return_4) override;

    void wait_for_non_mmio_flush() override;
    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    void deassert_risc_resets() override;
    void set_power_state(tt_DevicePowerState state) override;
    int get_clock() override;
    int get_numa_node() override;

    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channel) override;
};
}  // namespace tt::umd
