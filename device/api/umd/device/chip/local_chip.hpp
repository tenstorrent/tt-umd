// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "umd/device/chip/chip.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/robust_mutex.hpp"

namespace tt::umd {
class SocDescriptor;
class SysmemManager;
class TLBManager;

class LocalChip : public Chip {
public:
    static std::unique_ptr<LocalChip> create(std::unique_ptr<TTDevice> tt_device, int num_host_mem_channels = 0);

    ~LocalChip() override;

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
    void noc_multicast_write(
        const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;

    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) override;

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel = 0) override;

    void deassert_risc_resets() override;
    int get_clock() override;
    int get_numa_node() override;

    std::unique_lock<RobustMutex> acquire_mutex(const std::string& mutex_name, int pci_device_id);
    std::unique_lock<RobustMutex> acquire_mutex(MutexType mutex_type, int pci_device_id);

private:
    LocalChip(
        std::unique_ptr<TTDevice> tt_device,
        std::unique_ptr<TLBManager> tlb_manager,
        std::unique_ptr<SysmemManager> sysmem_manager);

    std::unique_ptr<TLBManager> tlb_manager_;
    std::unique_ptr<SysmemManager> sysmem_manager_;
    LockManager lock_manager_;

    // unique_lock is RAII, so if this member holds an object, the RobustMutex is locked, if it is empty, the
    // RobustMutex is unlocked.
    std::optional<std::unique_lock<RobustMutex>> chip_started_lock_;

    void initialize_tlb_manager();
    void initialize_default_chip_mutexes();
    void initialize_membars(uint32_t dram_subchannel);

    void init_pcie_iatus();

    void set_membar_flag(
        const std::vector<CoreCoord>& cores, const uint32_t barrier_value, const uint32_t barrier_addr);
    void insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr);

    std::unique_ptr<TTDevice> tt_device_ = nullptr;

    TlbWindow* get_cached_wc_tlb_window();
    TlbWindow* get_cached_uc_tlb_window();

    std::unique_ptr<TlbWindow> cached_wc_tlb_window = nullptr;
    std::unique_ptr<TlbWindow> cached_uc_tlb_window = nullptr;

    std::mutex wc_tlb_lock;
    std::mutex uc_tlb_lock;
};
}  // namespace tt::umd
