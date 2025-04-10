/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <unordered_set>

#include "umd/device/tt_soc_descriptor.h"
#include "umd/device/types/cluster_descriptor_types.h"
#include "umd/device/types/cluster_types.h"
#include "umd/device/utils/lock_manager.h"

namespace tt::umd {

class TTDevice;
class SysmemManager;
class TLBManager;

// An abstract class that represents a chip.
class Chip {
public:
    Chip(tt_SocDescriptor soc_descriptor);

    Chip(const ChipInfo chip_info, tt_SocDescriptor soc_descriptor);

    virtual ~Chip() = default;

    tt_SocDescriptor& get_soc_descriptor();

    virtual bool is_mmio_capable() const = 0;

    void set_barrier_address_params(const barrier_address_params& barrier_address_params_);

    const ChipInfo& get_chip_info();

    virtual TTDevice* get_tt_device();
    virtual SysmemManager* get_sysmem_manager();
    virtual TLBManager* get_tlb_manager();

    virtual void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size);
    virtual void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size);

    // TODO: Currently works only for Local and not for Remote.
    virtual void write_to_device(
        tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size, const std::string& fallback_tlb);
    virtual void read_from_device(
        tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size, const std::string& fallback_tlb);

    virtual void write_to_device_reg(
        tt_xy_pair core, const void* src, uint64_t reg_dest, uint32_t size, const std::string& fallback_tlb);
    virtual void read_from_device_reg(
        tt_xy_pair core, void* dest, uint64_t reg_src, uint32_t size, const std::string& fallback_tlb);

    // TODO: To be removed once all usages are moved inside local chip.
    virtual std::unique_lock<RobustMutex> acquire_mutex(std::string mutex_name, int pci_device_id);
    virtual std::unique_lock<RobustMutex> acquire_mutex(MutexType mutex_type, int pci_device_id);

    // TODO: This should be private, once enough stuff is moved inside chip.
    // Probably also moved to LocalChip.
    tt_device_dram_address_params dram_address_params;
    tt_device_l1_address_params l1_address_params;
    tt_driver_host_address_params host_address_params;
    tt_driver_noc_params noc_params;
    tt_driver_eth_interface_params eth_interface_params;

protected:
    void wait_chip_to_be_ready();

    virtual void wait_eth_cores_training(const uint32_t timeout_ms = 60000);

    virtual void wait_dram_cores_training(const uint32_t timeout_ms = 60000);

    void set_default_params(ARCH arch);

    ChipInfo chip_info_;

    tt_SocDescriptor soc_descriptor_;
};

}  // namespace tt::umd
