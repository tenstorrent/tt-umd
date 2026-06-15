// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

#include "umd/device/chip/chip.hpp"
// TODO : tt-metal uses SysmemBuffer transitively through this header. Remove once tt-metal includes it directly.
// Link to issue: https://github.com/tenstorrent/tt-umd/issues/2437.
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt {
struct EthCoord;
}  // namespace tt

namespace tt::umd {
class Chip;
class RemoteCommunication;
class SocDescriptor;

class RemoteChip : public Chip {
public:
    /** Create a RemoteChip instance.
     *
     * @param remote_tt_device An existing, initialized remote TTDevice.
     * @param local_chip The local chip to be used for communication to this remote chip.
     * @return A unique pointer to the created RemoteChip instance.
     */
    static std::unique_ptr<RemoteChip> create(std::unique_ptr<TTDevice> remote_tt_device, Chip* local_chip);
    static std::unique_ptr<RemoteChip> create_for_simulation(
        std::unique_ptr<TTDevice> remote_tt_device, Chip* local_chip, ChipInfo chip_info);

    bool is_mmio_capable() const override;

    void start_device(uint32_t dram_membar_subchannel = 0) override;
    void close_device() override;

    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    const SocDescriptor& get_soc_descriptor() const override { return tt_device_->get_soc_descriptor(); }

    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) override;
    int get_num_host_channels() override;
    int get_host_channel_size(std::uint32_t channel) override;
    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) override;
    void write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) override;
    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;
    void noc_multicast_write(
        const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel = 0) override;

    void deassert_risc_resets() override;
    int get_clock() override;
    int get_numa_node() override;

    RemoteCommunication* get_remote_communication();

private:
    RemoteChip(Chip* local_chip, std::unique_ptr<TTDevice> remote_tt_device);
    RemoteChip(Chip* local_chip, std::unique_ptr<TTDevice> remote_tt_device, ChipInfo chip_info);

    Chip* local_chip_;
    RemoteCommunication* remote_communication_;

    std::unique_ptr<TTDevice> tt_device_ = nullptr;
};
}  // namespace tt::umd
