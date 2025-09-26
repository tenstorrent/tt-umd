/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "chip_connection.hpp"
#include "pcie_specific_interface.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class PCIeConnection : public ChipConnection, public IPCIeSpecific {
public:
    PCIeConnection(TTDevice* tt_device, int num_host_mem_channels);

    virtual ~PCIeConnection();

    // General Chip Connection interface
    void write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) override;

    void write_to_device_reg(tt_xy_pair core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(tt_xy_pair core, void* dest, uint64_t reg_src, uint32_t size) override;

    void pre_initialization_hook() override;
    void initialization_hook() override;
    void post_initialization_hook() override;

    void verify_initialization() override;

    void start_connection() override;
    void stop_connection() override;

    void ethernet_broadcast_write(
        const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header) override;
    void set_remote_transfer_ethernet_cores(const std::unordered_set<tt_xy_pair>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) override;

    // Specific PCIe interface
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    int get_host_channel_size(std::uint32_t channel) override;

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) override;

    std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable() override;
    int get_numa_node() override;

private:
    void initialize_tlb_manager();
    void initialize_default_chip_mutexes();

    void check_pcie_device_initialized();
    int test_setup_interface();
    void init_pcie_iatus();

    int get_num_host_channels();

    std::unique_lock<RobustMutex> acquire_mutex(std::string mutex_name, int pci_device_id);
    std::unique_lock<RobustMutex> acquire_mutex(MutexType mutex_type, int pci_device_id);

    LockManager lock_manager_ = {};

    TTDevice* tt_device_ = nullptr;

    std::unique_ptr<TLBManager> tlb_manager_ = nullptr;
    std::unique_ptr<SysmemManager> sysmem_manager_ = nullptr;
    std::unique_ptr<RemoteCommunication> remote_communication_ = nullptr;
};

}  // namespace tt::umd
