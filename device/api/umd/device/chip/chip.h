/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <unordered_set>

#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_silicon_driver_common.hpp"
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

    virtual void start_device() = 0;
    virtual void close_device() = 0;

    tt_SocDescriptor& get_soc_descriptor();

    virtual bool is_mmio_capable() const = 0;

    void set_barrier_address_params(const barrier_address_params& barrier_address_params_);

    const ChipInfo& get_chip_info();

    virtual TTDevice* get_tt_device() = 0;
    virtual SysmemManager* get_sysmem_manager() = 0;
    virtual TLBManager* get_tlb_manager() = 0;

    virtual int get_num_host_channels() = 0;
    virtual int get_host_channel_size(std::uint32_t channel) = 0;
    virtual void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) = 0;
    virtual void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) = 0;

    virtual void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) = 0;
    virtual void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) = 0;
    virtual void write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) = 0;
    virtual void read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) = 0;
    virtual void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) = 0;
    virtual void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) = 0;

    virtual std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable() = 0;

    virtual void wait_for_non_mmio_flush() = 0;

    virtual void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) = 0;
    virtual void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) = 0;
    virtual void dram_membar(const std::unordered_set<uint32_t>& channels = {}) = 0;

    virtual void send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets);
    virtual void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets);
    virtual void deassert_risc_resets() = 0;

    virtual void set_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& selected_riscs);
    virtual void unset_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& selected_riscs);

    virtual void set_power_state(tt_DevicePowerState state) = 0;
    virtual int get_clock() = 0;
    virtual int get_numa_node() = 0;

    virtual int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        uint32_t timeout_ms = 1000,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);

    virtual void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) = 0;
    virtual void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channel) = 0;

    // TODO: To be moved to private implementation once methods are moved to chip
    void enable_ethernet_queue(int timeout_s);

    // TODO: This should be private, once enough stuff is moved inside chip.
    // Probably also moved to LocalChip.
    tt_device_dram_address_params dram_address_params;
    tt_device_l1_address_params l1_address_params;
    tt_driver_host_address_params host_address_params;
    tt_driver_noc_params noc_params;
    tt_driver_eth_interface_params eth_interface_params;

    // TODO: To be removed once we properly refactor usage of NOC1 coords.
    tt_xy_pair translate_chip_coord_to_translated(const CoreCoord core) const;

protected:
    void wait_chip_to_be_ready();

    virtual void wait_eth_cores_training(const uint32_t timeout_ms = 60000);

    virtual void wait_dram_cores_training(const uint32_t timeout_ms = 60000);

    void set_default_params(ARCH arch);

    uint32_t get_power_state_arc_msg(tt_DevicePowerState state);

    void wait_for_aiclk_value(TTDevice* tt_device, tt_DevicePowerState power_state, const uint32_t timeout_ms = 5000);

    ChipInfo chip_info_;

    tt_SocDescriptor soc_descriptor_;
};

}  // namespace tt::umd
