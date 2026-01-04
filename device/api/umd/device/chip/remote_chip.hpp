// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/chip/chip.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"

namespace tt::umd {
class LocalChip;

class RemoteChip : public Chip {
public:
    /** Create a RemoteChip instance.
     *
     * @param local_chip The local chip to be used for communication to this remote chip.
     * @param target_eth_coord The target Ethernet coordinates for the remote chip.
     * @param remote_transfer_eth_channels The set of Ethernet channels on local chip to use for remote communication.
     * @return A unique pointer to the created RemoteChip instance.
     */
    static std::unique_ptr<RemoteChip> create(
        LocalChip* local_chip,
        EthCoord target_eth_coord,
        std::set<uint32_t> remote_transfer_eth_channels,
        std::string sdesc_path = "");
    static std::unique_ptr<RemoteChip> create(
        LocalChip* local_chip,
        EthCoord target_eth_coord,
        std::set<uint32_t> remote_transfer_eth_channels,
        SocDescriptor soc_descriptor);

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

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels) override;

    void deassert_risc_resets(bool use_noc1) override;
    int get_clock() override;
    int get_numa_node() override;

    RemoteCommunication* get_remote_communication();

private:
    RemoteChip(SocDescriptor soc_descriptor, LocalChip* local_chip, std::unique_ptr<TTDevice> remote_tt_device);

    LocalChip* local_chip_;
    RemoteCommunication* remote_communication_;

    std::unique_ptr<TTDevice> tt_device_ = nullptr;
};
}  // namespace tt::umd
