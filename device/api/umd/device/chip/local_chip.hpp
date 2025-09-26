/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.hpp"
#include "umd/device/chip/chip_connection.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
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

    void verify_initialization() override;

    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) override;
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;
    void write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) override;
    void read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) override;

    void ethernet_broadcast_write(
        const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header);

    void wait_for_non_mmio_flush() override;

    void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    void deassert_risc_resets() override;
    void set_power_state(DevicePowerState state) override;
    int get_clock() override;

    std::unique_lock<RobustMutex> acquire_mutex(std::string mutex_name, int pci_device_id);
    std::unique_lock<RobustMutex> acquire_mutex(MutexType mutex_type, int pci_device_id);

private:
    LocalChip(tt_SocDescriptor soc_descriptor, std::unique_ptr<TTDevice> tt_device, int num_host_mem_channels);

    LockManager lock_manager_;

    // unique_lock is RAII, so if this member holds an object, the RobustMutex is locked, if it is empty, the
    // RobustMutex is unlocked.
    std::optional<std::unique_lock<RobustMutex>> chip_started_lock_;

    void initialize_tlb_manager();
    void initialize_default_chip_mutexes();
    void initialize_membars();

    void check_pcie_device_initialized();
    int test_setup_interface();
    void init_pcie_iatus();

    void set_membar_flag(
        const std::vector<CoreCoord>& cores, const uint32_t barrier_value, const uint32_t barrier_addr);
    void insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr);

    std::unique_ptr<TTDevice> tt_device_ = nullptr;
    std::unique_ptr<ChipConnection> chip_connection_ = nullptr;
};
}  // namespace tt::umd
