/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace tt::umd {

class LocalChip : public Chip {
public:
    // In some of the constructor implementations, we want to create TTDevice objects and then use them to obtain the
    // necessary information needed for soc descriptor construction. Due to this inverse member initialization order, we
    // cannot have simple constructors as they require the base class to be constructed first.
    static std::unique_ptr<LocalChip> create(
        int physical_device_id,
        std::string sdesc_path = "",
        int num_host_mem_channels = 0,
        IODeviceType device_type = IODeviceType::PCIe);
    static std::unique_ptr<LocalChip> create(
        int physical_device_id,
        SocDescriptor soc_descriptor,
        int num_host_mem_channels = 0,
        IODeviceType device_type = IODeviceType::PCIe);

    ~LocalChip();

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

    void safe_write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void safe_read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;
    void safe_write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void safe_read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) override;

    void ethernet_broadcast_write(
        const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header);

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    void deassert_risc_resets() override;
    int get_clock() override;
    int get_numa_node() override;

    std::unique_lock<RobustMutex> acquire_mutex(std::string mutex_name, int pci_device_id);
    std::unique_lock<RobustMutex> acquire_mutex(MutexType mutex_type, int pci_device_id);

private:
    LocalChip(
        SocDescriptor soc_descriptor,
        std::unique_ptr<TTDevice> tt_device,
        std::unique_ptr<TLBManager> tlb_manager,
        std::unique_ptr<SysmemManager> sysmem_manager,
        std::unique_ptr<RemoteCommunication> remote_communication,
        int num_host_mem_channels);

    std::unique_ptr<TLBManager> tlb_manager_;
    std::unique_ptr<SysmemManager> sysmem_manager_;
    LockManager lock_manager_;
    // Used only for ethernet broadcast to all remote chips.
    std::unique_ptr<RemoteCommunication> remote_communication_;

    // unique_lock is RAII, so if this member holds an object, the RobustMutex is locked, if it is empty, the
    // RobustMutex is unlocked.
    std::optional<std::unique_lock<RobustMutex>> chip_started_lock_;

    void initialize_tlb_manager();
    void initialize_default_chip_mutexes();
    void initialize_membars();

    template <bool safe>
    void write_to_device_impl(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size);

    template <bool safe>
    void read_from_device_impl(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size);

    template <bool safe>
    void write_to_device_reg_impl(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size);

    template <bool safe>
    void read_from_device_reg_impl(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size);

    void check_pcie_device_initialized();
    int test_setup_interface();
    void init_pcie_iatus();

    void set_membar_flag(
        const std::vector<CoreCoord>& cores, const uint32_t barrier_value, const uint32_t barrier_addr);
    void insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr);

    std::unique_ptr<TTDevice> tt_device_ = nullptr;

    TlbWindow* get_cached_wc_tlb_window(tlb_data config);
    TlbWindow* get_cached_uc_tlb_window(tlb_data config);
    TlbWindow* get_cached_pcie_dma_tlb_window(tlb_data config);

    std::unique_ptr<TlbWindow> cached_wc_tlb_window = nullptr;
    std::unique_ptr<TlbWindow> cached_uc_tlb_window = nullptr;
    std::unique_ptr<TlbWindow> cached_pcie_dma_tlb_window = nullptr;

    std::mutex wc_tlb_lock;
    std::mutex uc_tlb_lock;
    std::mutex pcie_dma_lock;
};
}  // namespace tt::umd
