// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <unordered_set>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class TTDevice;
class SysmemManager;
class TLBManager;

// An abstract class that represents a chip.
class Chip {
public:
    Chip(SocDescriptor soc_descriptor);

    Chip(const ChipInfo chip_info, SocDescriptor soc_descriptor);

    virtual ~Chip() = default;

    virtual void start_device() = 0;
    virtual void close_device() = 0;

    SocDescriptor& get_soc_descriptor();

    virtual bool is_mmio_capable() const = 0;

    void set_barrier_address_params(const BarrierAddressParams& barrier_address_params);

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
    virtual void dma_multicast_write(
        void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) = 0;
    virtual void noc_multicast_write(void* dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr);

    virtual void wait_for_non_mmio_flush() = 0;

    virtual void l1_membar(const std::unordered_set<CoreCoord>& cores = {}) = 0;
    virtual void dram_membar(const std::unordered_set<CoreCoord>& cores = {}) = 0;
    virtual void dram_membar(const std::unordered_set<uint32_t>& channels) = 0;

    // TODO: Remove this API once we switch to the new one.
    virtual void send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets);
    virtual void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets);
    virtual void deassert_risc_resets() = 0;

    /**
    Returns a set of riscs which have soft reset signal raised (these riscs are in reset state).
    */
    virtual RiscType get_risc_reset_state(CoreCoord core);

    /**
    Assert the soft reset signal for specified riscs on the specified core.
    Raising this signal will put those riscs in the reset state and stop their execution.
    */
    virtual void assert_risc_reset(CoreCoord core, const RiscType selected_riscs);

    /**
    Deassert the soft reset signal for specified riscs on the specified core.
    Lowering this signal will put those riscs in the running state and start their execution.
    */
    virtual void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start);

    /**
    Assert the soft reset signal for specified riscs on all cores.
    Raising this signal will put those riscs in the reset state and stop their execution.
    */
    virtual void assert_risc_reset(const RiscType selected_riscs);

    /**
    Deassert the soft reset signal for specified riscs on all cores.
    Lowering this signal will put those riscs in the running state and start their execution.
    */
    virtual void deassert_risc_reset(const RiscType selected_riscs, bool staggered_start);

    virtual void set_power_state(DevicePowerState state);
    virtual int get_clock() = 0;
    virtual int get_numa_node() = 0;

    virtual int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);

    virtual void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) = 0;
    virtual void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) = 0;

    // TODO: To be moved to private implementation once methods are moved to chip.
    void enable_ethernet_queue(const std::chrono::milliseconds timeout_ms = timeout::ETH_QUEUE_ENABLE_TIMEOUT);

    // TODO: This should be private, once enough stuff is moved inside chip.
    // Probably also moved to LocalChip.
    DeviceDramAddressParams dram_address_params;
    DeviceL1AddressParams l1_address_params;

protected:
    void wait_chip_to_be_ready();

    virtual void wait_eth_cores_training(const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);

    virtual void wait_dram_cores_training(const std::chrono::milliseconds timeout_ms = timeout::DRAM_TRAINING_TIMEOUT);

    void set_default_params(ARCH arch);

    uint32_t get_power_state_arc_msg(DevicePowerState state);

    void wait_for_aiclk_value(
        TTDevice* tt_device,
        DevicePowerState power_state,
        const std::chrono::milliseconds timeout_ms = timeout::AICLK_TIMEOUT);

    ChipInfo chip_info_;

    SocDescriptor soc_descriptor_;
};

}  // namespace tt::umd
