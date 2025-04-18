/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"
#include "umd/device/chip_helpers/sysmem_manager.h"
#include "umd/device/chip_helpers/tlb_manager.h"
#include "umd/device/remote_communication.h"

namespace tt::umd {

class LocalChip : public Chip {
public:
    LocalChip(
        tt_SocDescriptor soc_descriptor,
        int pci_device_id,
        int num_host_mem_channels = 0,
        const bool clear_mutex = false);

    LocalChip(std::string sdesc_path, std::unique_ptr<TTDevice> tt_device);

    LocalChip(std::unique_ptr<TTDevice> tt_device);

    bool is_mmio_capable() const override;

    void start_device() override;

    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    // TODO: To be removed once all the usages are moved inside the class.
    tt_xy_pair get_remote_transfer_ethernet_core() override;
    void update_active_eth_core_idx() override;
    int get_active_eth_core_idx() override;
    std::vector<CoreCoord> get_remote_transfer_ethernet_cores() override;

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    void write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) override;
    void write_to_device_reg(tt_xy_pair core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(tt_xy_pair core, void* dest, uint64_t reg_src, uint32_t size) override;

    void dma_write_to_device(const void* src, size_t size, tt_xy_pair core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, tt_xy_pair core, uint64_t addr) override;

    void ethernet_broadcast_write(
        const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header);

    void wait_for_non_mmio_flush() override;
    void set_flush_non_mmio(bool flush_non_mmio);
    bool get_flush_non_mmio() const;

    void l1_membar(const std::unordered_set<tt::umd::CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<tt::umd::CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    std::unique_lock<RobustMutex> acquire_mutex(std::string mutex_name, int pci_device_id) override;
    std::unique_lock<RobustMutex> acquire_mutex(MutexType mutex_type, int pci_device_id) override;

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        uint32_t timeout_ms = 1000,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) override;

private:
    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<SysmemManager> sysmem_manager_;
    std::unique_ptr<TLBManager> tlb_manager_;
    LockManager lock_manager_;
    // Used only for ethernet broadcast to all remote chips.
    std::unique_ptr<RemoteCommunication> remote_communication_;

    std::vector<CoreCoord> remote_transfer_eth_cores_;
    int active_eth_core_idx = 0;
    bool flush_non_mmio_ = false;

    void initialize_local_chip(int num_host_mem_channels = 0, const bool clear_mutex = false);
    void initialize_tlb_manager();
    void initialize_default_chip_mutexes(const bool clear_mutex);
    void initialize_default_remote_transfer_ethernet_cores();
    void initialize_membars();

    tt_xy_pair translate_chip_coord_virtual_to_translated(const tt_xy_pair core) const;

    void check_pcie_device_initialized();
    int test_setup_interface();
    void init_pcie_iatus();

    void set_membar_flag(
        const std::vector<CoreCoord>& cores, const uint32_t barrier_value, const uint32_t barrier_addr);
    void insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr);

protected:
    void wait_eth_cores_training(const uint32_t timeout_ms = 60000) override;

    void wait_dram_cores_training(const uint32_t timeout_ms = 60000) override;
};
}  // namespace tt::umd
