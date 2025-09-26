/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <set>

#include "chip_helpers/tlb_manager.hpp"
#include "pci_device.hpp"
#include "tt_io.hpp"
#include "umd/device/chip/chip.hpp"
#include "umd/device/chip/pcie_connection.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class IClusterPcie {
public:
    IClusterPcie(std::set<chip_id_t>& local_chip_ids, std::unordered_map<chip_id_t, std::unique_ptr<Chip>>& chips);

    // this can be set or something similar
    void initialize_pcie_chips();

    /**
     * Configure a TLB to point to a specific core and an address within that core. Should be done for Static TLBs.
     * If the device uses another mechanism for providing access to the host, this can be ignored.
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param logical_device_id Logical Device being targeted.
     * @param core The TLB will be programmed to point to this core.
     * @param tlb_index TLB id that will be programmed.
     * @param address Start address TLB is mapped to.
     * @param ordering Ordering mode for the TLB.
     */
    void configure_tlb(
        chip_id_t logical_device_id,
        tt_xy_pair core,
        int32_t tlb_index,
        uint64_t address,
        uint64_t ordering = tlb_data::Relaxed);

    /**
     * Configure a TLB to point to a specific core and an address within that core. Should be done for Static TLBs.
     * If the device uses another mechanism for providing access to the host, this can be ignored.
     *
     * @param logical_device_id Logical Device being targeted.
     * @param core The TLB will be programmed to point to this core.
     * @param tlb_index TLB id that will be programmed.
     * @param address Start address TLB is mapped to.
     * @param ordering Ordering mode for the TLB.
     */
    void configure_tlb(
        chip_id_t logical_device_id,
        CoreCoord core,
        int32_t tlb_index,
        uint64_t address,
        uint64_t ordering = tlb_data::Relaxed);

    /**
     * Use PCIe DMA to write device memory (L1 or DRAM).
     *
     * @param src Source data address.
     * @param size Size in bytes.
     * @param chip Chip to target; must be local, i.e. attached via PCIe.
     * @param core Core to target.
     * @param addr Address to write to.
     */
    void dma_write_to_device(const void* src, size_t size, chip_id_t chip, CoreCoord core, uint64_t addr);

    /**
     * Use PCIe DMA to read device memory (L1 or DRAM).
     *
     * @param src Source data address.
     * @param size Size in bytes.
     * @param chip Chip to target; must be local, i.e. attached via PCIe.
     * @param core Core to target.
     * @param addr Address to read from.
     */
    void dma_read_from_device(void* dst, size_t size, chip_id_t chip, CoreCoord core, uint64_t addr);

    /**
     * This API allows you to write directly to device memory that is addressable by a static TLB.
     */
    std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable(int device_id);

    /**
     * Provide fast write access to a statically-mapped TLB.
     * It is the caller's responsibility to ensure that
     * - the target has a static TLB mapping configured.
     * - the mapping is unchanged during the lifetime of the returned object.
     * - the Cluster instance outlives the returned object.
     * - use of the returned object is congruent with the target's TLB setup.
     *
     * @param target The target chip and core to write to.
     */
    Writer get_static_tlb_writer(const chip_id_t chip, const CoreCoord core);

    /**
     * Write data to specified address and channel on host (defined for Silicon).
     * This API is used to write to the host memory location that is made available to the device through
     * initialization. During the initialization the user should be able to specify how many "channels" are available to
     * the device, and that is what the channel argument refers to. This API can be directed to memory on the device
     * itself if needed. That would imply some performance considerations.
     *
     * @param mem_ptr Data to write.
     * @param size Number of bytes to write.
     * @param addr Address to write to.
     * @param channel Host channel to target.
     * @param src_device_id Chip to target.
     */
    void write_to_sysmem(
        const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id);

    /**
     * Read data from specified address and channel on host (defined for Silicon).
     * Similar as write_to_sysmem, but for reading.
     *
     * @param vec Data to write.
     * @param addr Address to write to.
     * @param channel Host channel to target.
     * @param size Number of bytes to read.
     * @param src_device_id Chip to target.
     */
    void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);

    /**
     * Query number of memory channels on Host device allocated for a specific device during initialization.
     *
     * @param device_id Logical device id to target.
     */
    std::uint32_t get_num_host_channels(std::uint32_t device_id);

    /**
     * Get size for a specific Host channel accessible by the corresponding device.
     *
     * @param device_id Logical device id to target.
     * @param channel Logical host channel to target.
     */
    std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);

    /**
     * Get absolute address corresponding to a zero based offset into a specific host memory channel for a specific
     * device.
     *
     * @param offset Offset wrt the start of the channel's address space.
     * @param src_device_id Device to target.
     * @param channel Host memory channel.
     */
    void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;

    /**
     * Get base PCIe address that is used to access the device.
     *
     * @param chip_id Chip to target.
     */
    std::uint64_t get_pcie_base_addr_from_device(const chip_id_t chip_id) const;

    /**
     * Get which NUMA node this device is associated with, or -1 if non-NUMA
     *
     * @param device_id Logical device id to query.
     */
    std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id);

    /**
     * Get PCI device for specified logical device id.
     *
     * @param device_id Device to target.
     */
    PCIDevice* get_pci_device(int device_id) const;

    /**
     * Get TLBManager for specified logical device id.
     *
     * @param device_id Device to target.
     */
    TLBManager* get_tlb_manager(chip_id_t device_id) const;

    /**
     * Exposes how TLBs are configured for a specific device.
     */
    tlb_configuration get_tlb_configuration(const chip_id_t chip, const CoreCoord core);

private:
    bool verify_sysmem_initialized(chip_id_t chip_id);

    std::set<chip_id_t>& local_chip_ids_;
    std::unordered_map<chip_id_t, std::unique_ptr<Chip>>& chips_;
    std::unordered_map<chip_id_t, PCIeConnection*> pcie_chips_;
};

}  // namespace tt::umd
