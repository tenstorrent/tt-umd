/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"
#include "umd/device/remote_communication.h"
#include "umd/device/tt_device/remote_wormhole_tt_device.h"

namespace tt::umd {
class LocalChip;

class RemoteChip : public Chip {
public:
    static std::unique_ptr<RemoteChip> create(LocalChip* local_chip, eth_coord_t target_eth_coord);
    static std::unique_ptr<RemoteChip> create(
        LocalChip* local_chip, eth_coord_t target_eth_coord, std::string sdesc_path);
    static std::unique_ptr<RemoteChip> create(
        LocalChip* local_chip, eth_coord_t target_eth_coord, tt_SocDescriptor soc_descriptor);

    bool is_mmio_capable() const override;

    void start_device() override;
    void close_device() override;

    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) override;
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

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    void deassert_risc_resets() override;
    void set_power_state(tt_DevicePowerState state) override;
    int get_clock() override;
    int get_numa_node() override;

private:
    RemoteChip(
        tt_SocDescriptor soc_descriptor,
        LocalChip* local_chip,
        std::unique_ptr<RemoteWormholeTTDevice> remote_tt_device);

    LocalChip* local_chip_;
    RemoteCommunication* remote_communication_;

    std::unique_ptr<TTDevice> tt_device_ = nullptr;
};
}  // namespace tt::umd
