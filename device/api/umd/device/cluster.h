/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "fmt/core.h"
#include "tt_silicon_driver_common.hpp"
#include "umd/device/blackhole_arc_message_queue.h"
#include "umd/device/chip/chip.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_io.hpp"
#include "umd/device/types/arch.h"
#include "umd/device/types/cluster_descriptor_types.h"
#include "umd/device/types/cluster_types.h"
#include "umd/device/types/tlb.h"

using TLB_DATA = tt::umd::tlb_data;

namespace boost::interprocess {
class named_mutex;
}

class tt_ClusterDescriptor;

// TODO: This class is to be removed once we move Simulation and Mockup devices to be Chips instead of Clusters.
/**
 * Parent class for Cluster (Silicon Driver).
 * Exposes a generic interface to callers, providing declarations for virtual functions defined differently for Silicon.
 * Valid usage consists of declaring a tt_device object and initializing it to Silicon backend.
 * Using tt_device itself will throw errors, since its APIs are undefined.
 */
class tt_device {
public:
    /**
     * The constructor of the derived tt_device should perform everything important for initializing the device
     * properly. This can include, but is not limited to:
     * - Getting the base address for the Device which is to be used when accessing it through the API, including memory
     * mapping the device address space.
     * - Setting up security access (if any).
     * - Establishing a link to the kernel module driver (if any).
     * - Additional setup needed for read/write operation from the device. DMA setup (if any).
     * - Allocating system memory that the device has access to.
     * - Setup access to DRAM module.
     * - Create SoCDescriptors from passed custom soc descriptor yaml path.
     * - Perform this for each of the chips connected to the system.
     */
    tt_device(){};

    /**
     * Closing the device. Should undo everything that was done in the constructor. Break created links, free memory,
     * leave the device in a state where it can be re-initialized.
     */
    virtual ~tt_device(){};

    // Setup/Teardown Functions
    /**
     * Set Barrier Address Map parameters used by UMD to communicate with the TT Device.
     * This function should be called right after the device is created. This sets up barrier addresses for tensix L1,
     * eth L1, and DRAM. Barrier addresses are used when calling l1_membar, dram_membar and wait_for_non_mmio_flush.
     * These need to be setup only for the synchronisation purposes between the host and the device.
     *
     * @param barrier_address_params_  All the barrier parameters required by UMD
     */
    virtual void set_barrier_address_params(const barrier_address_params& barrier_address_params_) {
        throw std::runtime_error("---- tt_device::set_barrier_address_params is not implemented\n");
    }

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
    virtual void configure_tlb(
        chip_id_t logical_device_id,
        tt_xy_pair core,
        int32_t tlb_index,
        uint64_t address,
        uint64_t ordering = TLB_DATA::Relaxed) {
        throw std::runtime_error("---- tt_device::configure_tlb is not implemented\n");
    }

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
    virtual void configure_tlb(
        chip_id_t logical_device_id,
        tt::umd::CoreCoord core,
        int32_t tlb_index,
        uint64_t address,
        uint64_t ordering = TLB_DATA::Relaxed) {
        throw std::runtime_error("---- tt_device::configure_tlb is not implemented\n");
    }

    /**
     * Set ordering mode for dynamic/fallback TLBs (passed into driver constructor).
     *
     * @param fallback_tlb Dynamic TLB being targeted.
     * @param ordering Ordering mode for the TLB.
     */
    virtual void set_fallback_tlb_ordering_mode(const std::string& fallback_tlb, uint64_t ordering = TLB_DATA::Posted) {
        throw std::runtime_error("---- tt_device::set_fallback_tlb_ordering_mode is not implemented\n");
    }

    /**
     * Pass in ethernet cores with active links for a specific MMIO chip. When called, this function will force UMD to
     * use a subset of cores from the active_eth_cores_per_chip set for all host->cluster non-MMIO transfers. If this
     * function is not called, UMD will use a default set of ethernet core indices for these transfers (0 through 5). If
     * default behaviour is not desired, this function must be called for all MMIO devices.
     *
     * @param mmio_chip Device being targeted.
     * @param active_eth_cores_per_chip The active ethernet cores for this chip.
     */
    virtual void configure_active_ethernet_cores_for_mmio_device(
        chip_id_t mmio_chip, const std::unordered_set<tt_xy_pair>& active_eth_cores_per_chip) {
        throw std::runtime_error(
            "---- tt_device::configure_active_ethernet_cores_for_mmio_device is not implemented\n");
    }

    /**
     * Pass in ethernet cores with active links for a specific MMIO chip. When called, this function will force UMD to
     * use a subset of cores from the active_eth_cores_per_chip set for all host->cluster non-MMIO transfers. If this
     * function is not called, UMD will use a default set of ethernet core indices for these transfers (0 through 5). If
     * default behaviour is not desired, this function must be called for all MMIO devices.
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param active_eth_cores_per_chip The active ethernet cores for this chip.
     * @param mmio_chip Device being targeted.
     */
    virtual void configure_active_ethernet_cores_for_mmio_device(
        const std::unordered_set<tt::umd::CoreCoord>& active_eth_cores_per_chip, chip_id_t mmio_chip) {
        throw std::runtime_error(
            "---- tt_device::configure_active_ethernet_cores_for_mmio_device is not implemented\n");
    }

    /**
     * This function puts the device in a state so that it is ready for loading kernels to the tensix cores.
     * Can include, but is not limited to:
     * - Assert soft Tensix reset
     * - Deassert RiscV reset
     * - Set power state to busy (ramp up AICLK)
     * - Initialize iATUs for PCIe devices
     * - Initialize ethernet queues for remote chips.
     *
     * @param device_params Object specifying initialization configuration.
     */
    virtual void start_device(const tt_device_params& device_params) {
        throw std::runtime_error("---- tt_device::start_device is not implemented\n");
    }

    /**
     * Broadcast deassert BRISC soft Tensix Reset to the entire device.
     * This function needs to be called after start_device.
     * It writes to TENSIX register SOFT_RESET, the address of
     * which is architecture dependant. Please consult the desired architecture specs to find the exact address
     */
    virtual void deassert_risc_reset() {
        throw std::runtime_error("---- tt_device::deassert_risc_reset is not implemented\n");
    }

    /**
     * Send a BRISC soft deassert reset signal to a single tensix core.
     * Similar to the broadcast deassert_risc_reset API function, but done only on a single core.
     *
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param core Chip and Core to target.
     * @param soft_resets Specifies which RISCV cores on Tensix to deassert.
     */
    virtual void deassert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::deassert_risc_reset_at_core is not implemented\n");
    }

    /**
     * Send a BRISC soft deassert reset signal to a single tensix core.
     * Similar to the broadcast deassert_risc_reset API function, but done only on a single core.
     *
     * @param chip Chip to target.
     * @param core Core to target.
     * @param soft_resets Specifies which RISCV cores on Tensix to deassert.
     */
    virtual void deassert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::deassert_risc_reset_at_core is not implemented\n");
    }

    /**
     * Broadcast BRISC assert BRISC soft Tensix Reset to the entire device.
     * It writes to TENSIX register SOFT_RESET, the address of
     * which is architecture dependant. Please consult the desired architecture specs to find the exact address
     */
    virtual void assert_risc_reset() {
        throw std::runtime_error("---- tt_device::assert_risc_reset is not implemented\n");
    }

    /**
     * Send a BRISC soft assert reset signal to a single tensix core.
     * It writes to TENSIX register SOFT_RESET, the address of
     * which is architecture dependant. Please consult the desired architecture specs to find the exact address
     *
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param core Chip and Core to target.
     * @param soft_resets Specifies which RISCV cores on Tensix to deassert.
     */
    virtual void assert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::assert_risc_reset_at_core is not implemented\n");
    }

    /**
     * Send a BRISC soft assert reset signal to a single tensix core.
     * It writes to TENSIX register SOFT_RESET, the address of
     * which is architecture dependant. Please consult the desired architecture specs to find the exact address
     *
     * @param core Chip to target.
     * @param core Core to target.
     * @param soft_resets Specifies which RISCV cores on Tensix to deassert.
     */
    virtual void assert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::assert_risc_reset_at_core is not implemented\n");
    }

    /**
     * To be called at the end of a run.
     * Can include, but not limited to:
     * - Setting power state to idle
     * - Assert tensix reset at all cores.
     */
    virtual void close_device() { throw std::runtime_error("---- tt_device::close_device is not implemented\n"); }

    // Runtime functions
    /**
     * Non-MMIO (ethernet) barrier.
     * Similar to an mfence for host -> host transfers. Will flush all in-flight ethernet transactions before proceeding
     * with the next one. This will be applied to all chips in the cluster.
     *
     * This function is only used in context of remote (ethernet connected) chips in the cluster.
     */
    virtual void wait_for_non_mmio_flush() {
        throw std::runtime_error("---- tt_device::wait_for_non_mmio_flush is not implemented\n");
    }

    /**
     * Non-MMIO (ethernet) barrier.
     * This function should be called for a remote chip. If called for local chip, it will be a no-op.
     * This function is only used in context of remote (ethernet connected) chips in the cluster.
     *
     * @param chip_id Chip to target.
     */
    virtual void wait_for_non_mmio_flush(const chip_id_t chip_id) {
        throw std::runtime_error("---- tt_device::wait_for_non_mmio_flush is not implemented\n");
    }

    /**
     * Write uint32_t data (as specified by ptr + len pair) to specified device, core and address (defined for Silicon).
     * This API is used for writing to both TENSIX and DRAM cores. The internal SocDescriptor can be used to determine
     * which type of the core is being targeted.
     *
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param mem_ptr Source data address.
     * @param size_in_bytes Source data size.
     * @param core Device and Core to target.
     * @param addr Address to write to.
     * @param tlb_to_use Specifies fallback/dynamic TLB to use.
     */
    virtual void write_to_device(
        const void* mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

    /**
     * Write uint32_t data (as specified by ptr + len pair) to specified device, core and address (defined for Silicon).
     * This API is used for writing to both TENSIX and DRAM cores. The internal SocDescriptor can be used to determine
     * which type of the core is being targeted.
     *
     * @param mem_ptr Source data address.
     * @param size_in_bytes Source data size.
     * @param chip Chip to target.
     * @param core Core to target.
     * @param addr Address to write to.
     * @param tlb_to_use Specifies fallback/dynamic TLB to use.
     */
    virtual void write_to_device(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        chip_id_t chip,
        tt::umd::CoreCoord core,
        uint64_t addr,
        const std::string& tlb_to_use) {
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

    /**
     * This function writes to multiple chips and cores in the cluster. A set of chips, rows and columns can be excluded
     * from the broadcast. The function has to be called either only for Tensix cores or only for DRAM cores.
     *
     *
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param mem_ptr Data to write.
     * @param size_in_bytes Size of data to write.
     * @param address Address to write to.
     * @param chips_to_exclude Chips to exclude from the broadcast.
     * @param rows_to_exclude  NOC0 rows to exclude from the broadcast.
     * @param columns_to_exclude NOC0 columns to exclude from the broadcast.
     * @param fallback_tlb Specifies fallback/dynamic TLB to use.
     */
    virtual void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude,
        const std::string& fallback_tlb) {
        throw std::runtime_error("---- tt_device::broadcast_write_to_cluster is not implemented\n");
    }

    /**
     * Read uint32_t data from a specified device, core and address to host memory (defined for Silicon).
     * This API is used for reading from both TENSIX and DRAM cores. The internal SocDescriptor can be used to determine
     * which type of the core is being targeted.
     *
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param mem_ptr Data pointer to read the data into.
     * @param core Chip and Core to target.
     * @param addr Address to read from.
     * @param size Number of bytes to read.
     * @param fallback_tlb Specifies fallback/dynamic TLB to use.
     */
    virtual void read_from_device(
        void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

    /**
     * Read uint32_t data from a specified device, core and address to host memory (defined for Silicon).
     * This API is used for reading from both TENSIX and DRAM cores. The internal SocDescriptor can be used to determine
     * which type of the core is being targeted.
     *
     * @param mem_ptr Data pointer to read the data into.
     * @param chip Chip to target.
     * @param core Core to target.
     * @param addr Address to read from.
     * @param size Number of bytes to read.
     * @param fallback_tlb Specifies fallback/dynamic TLB to use.
     */
    virtual void read_from_device(
        void* mem_ptr,
        chip_id_t chip,
        tt::umd::CoreCoord core,
        uint64_t addr,
        uint32_t size,
        const std::string& fallback_tlb) {
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

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
    virtual void write_to_sysmem(
        const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::write_to_sysmem is not implemented\n");
    }

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
    virtual void read_from_sysmem(
        void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::read_from_sysmem is not implemented\n");
    }

    /**
     * Tensix L1 memory barrier.
     * This should be called when the client wants to ensure that all transactions on the L1 of the specified cores have
     * completed.
     *
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param chip Chip to target.
     * @param flackback_tlb Specifies fallback/dynamic TLB to use.
     * @param cores Cores being targeted.
     */
    virtual void l1_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {}) {
        throw std::runtime_error("---- tt_device::l1_membar is not implemented\n");
    }

    /**
     * Tensix L1 memory barrier.
     * This should be called when the client wants to ensure that all transactions on the L1 of the specified cores have
     * completed.
     *
     * @param chip Chip to target.
     * @param cores Cores being targeted.
     * @param flackback_tlb Specifies fallback/dynamic TLB to use.
     */
    virtual void l1_membar(
        const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores, const std::string& fallback_tlb) {
        throw std::runtime_error("---- tt_device::l1_membar is not implemented\n");
    }

    /**
     * DRAM memory barrier.
     * This should be called when the client wants to ensure that all transactions on the specified dram bank have
     * completed.
     *
     * @param chip Chip to target.
     * @param flackback_tlb Specifies fallback/dynamic TLB to use.
     * @param channels Channels being targeted.
     */
    virtual void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    /**
     * DRAM memory barrier.
     * This should be called when the client wants to ensure that all transactions on the specified dram bank have
     * completed.
     *
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param chip Chip to target.
     * @param flackback_tlb Specifies fallback/dynamic TLB to use.
     * @param cores Cores being targeted.
     */
    virtual void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    /**
     * DRAM memory barrier.
     * This should be called when the client wants to ensure that all transactions on the specified dram bank have
     * completed.
     *
     * @param chip Chip being targeted.
     * @param cores Cores being targeted.
     * @param flackback_tlb Specifies fallback/dynamic TLB to use.
     */
    virtual void dram_membar(
        const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores, const std::string& fallback_tlb) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    // Misc. Functions to Query/Set Device State
    /**
     * Query post harvesting SOC descriptors from UMD in virtual coordinates.
     * It should just return the SoCDescriptors that were created during construction.
     * These descriptors should be used for looking up cores that are passed into UMD APIs.
     */
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor> get_virtual_soc_descriptors() {
        return soc_descriptor_per_chip;
    }

    /**
     * Determine if UMD performed harvesting on SOC descriptors. Returns false if there is no harvesting for the
     * devices.
     */
    virtual bool using_harvested_soc_descriptors() {
        throw std::runtime_error("---- tt_device:using_harvested_soc_descriptors is not implemented\n");
    }

    /**
     * Get harvesting masks for all chips/SOC Descriptors in the cluster.
     * Each mask represents a map of enabled (0) and disabled (1) rows on a specific chip (in NOC0 Coordinateds).
     * Returns a map which has all zeros if there is no harvesting for these devices.
     */
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors() {
        throw std::runtime_error("---- tt_device:get_harvesting_masks_for_soc_descriptors is not implemented\n");
    }

    /**
     * Issue message to device, meant to be picked up by ARC firmware.
     *
     * @param logical_device_id Chip to target.
     * @param msg_code Specifies type of ARC message.
     * @param wait_for_done Block until ARC responds.
     * @param arg0 Message related argument.
     * @param arg1 Message related argument.
     * @param timeout Timeout on ARC.
     * @param return3 Return value from ARC.
     * @param return4 Return value from ARC.
     */
    virtual int arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) {
        throw std::runtime_error("---- tt_device::arc_msg is not implemented\n");
    }

    /**
     * Translate between virtual coordinates (from UMD SOC Descriptor) and translated coordinates.
     * This function is a no-op if no harvesting or translation are available on the device.
     *
     * @param device_id Chip to target.
     * @param r Row coordinate.
     * @param c Column coordinate.
     */
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c) {
        throw std::runtime_error("---- tt_device::translate_to_noc_table_coords is not implemented\n");
    }

    /**
     * Get cluster descriptor object being used in UMD instance.
     */
    virtual tt_ClusterDescriptor* get_cluster_description() {
        throw std::runtime_error("---- tt_device::get_cluster_description is not implemented\n");
    }

    /**
     * Get set of chip ids for all chips in the cluster.
     */
    virtual std::set<chip_id_t> get_target_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_device_ids is not implemented\n");
    }

    /**
     * Get all logical ids for all local chips targeted by UMD.
     */
    virtual std::set<chip_id_t> get_target_mmio_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_mmio_device_ids is not implemented\n");
    }

    /**
     * Get all logical ids for all Ethernet Mapped chips targeted by UMD.
     * Returns an empty set if no remote chips exist in the cluster.
     */
    virtual std::set<chip_id_t> get_target_remote_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_remote_device_ids is not implemented\n");
    }

    /**
     * Get clock frequencies for all MMIO devices targeted by UMD.
     */
    virtual std::map<int, int> get_clocks() {
        throw std::runtime_error("---- tt_device::get_clocks is not implemented\n");
    }

    /**
     * Get which NUMA node this device is associated with, or -1 if non-NUMA
     *
     * @param device_id Logical device id to query.
     */
    virtual std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id) {
        throw std::runtime_error("---- tt_device::get_numa_node_for_pcie_device is not implemented\n");
    }

    /**
     * Get the ethernet firmware version used by the physical cluster (only implemented for Silicon Backend).
     * Will return a bogus version if no remote chips are supported for the device.
     */
    virtual tt_version get_ethernet_fw_version() const {
        throw std::runtime_error("---- tt_device::get_ethernet_fw_version is not implemented \n");
    }

    /**
     * Query number of DRAM channels on a specific device.
     *
     * @param device_id Logical device id to query.
     */
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id) {
        throw std::runtime_error("---- tt_device::get_num_dram_channels is not implemented\n");
    }

    /**
     * Get size for a specific DRAM channel on a device.
     *
     * @param device_id Device to target.
     * @param channel DRAM channel to target.
     */
    virtual std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
        throw std::runtime_error("---- tt_device::get_dram_channel_size is not implemented\n");
    }

    /**
     * Query number of memory channels on Host device allocated for a specific device during initialization.
     *
     * @param device_id Logical device id to target.
     */
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id) {
        throw std::runtime_error("---- tt_device::get_num_host_channels is not implemented\n");
    }

    /**
     * Get size for a specific Host channel accessible by the corresponding device.
     *
     * @param device_id Logical device id to target.
     * @param channel Logical host channel to target.
     */
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
        throw std::runtime_error("---- tt_device::get_host_channel_size is not implemented\n");
    }

    /**
     * Get absolute address corresponding to a zero based offset into a specific host memory channel for a specific
     * device.
     *
     * @param offset Offset wrt the start of the channel's address space.
     * @param src_device_id Device to target.
     * @param channel Host memory channel.
     */
    virtual void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
        throw std::runtime_error("---- tt_device::host_dma_address is not implemented\n");
    }

    /**
     * Get base PCIe address that is used to access the device.
     *
     * @param chip_id Chip to target.
     */
    virtual std::uint64_t get_pcie_base_addr_from_device(const chip_id_t chip_id) const {
        throw std::runtime_error("---- tt_device::get_pcie_base_addr_from_device is not implemented\n");
    }

    /**
     * Get soc descriptor for specified chip.
     *
     * @param chip_id Chip to get soc descriptor for.
     */
    virtual const tt_SocDescriptor& get_soc_descriptor(chip_id_t chip_id) const {
        return soc_descriptor_per_chip.at(chip_id);
    }

    bool performed_harvesting = false;
    std::unordered_map<chip_id_t, uint32_t> harvested_rows_per_target = {};
    bool translation_tables_en = false;

protected:
    // TODO: Remove this once get_virtual_soc_descriptors can be removed.
    std::unordered_map<chip_id_t, tt_SocDescriptor> soc_descriptor_per_chip = {};
};

namespace tt::umd {

/**
 * Silicon Driver Class, derived from the tt_device class
 * Implements APIs to communicate with a physical Tenstorrent Device.
 */
class Cluster : public tt_device {
public:
    // Constructor
    /**
     * Cluster constructor.
     * Simplest form, creates a cluster of all available devices on the system.
     *
     * @param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages).
     * @param skip_driver_allocs
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks Manually specify additional harvesting masks for the devices in the cluster.
     * The ones defined by the devices itself have to be used, they will be merged with the ones passed here.
     */
    Cluster(
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool skip_driver_allocs = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {});

    /**
     * Cluster constructor.
     * This constructor can be used to target only specific devices on the system.
     *
     * @param target_devices Devices to target.
     * @param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages).
     * @param skip_driver_allocs
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks Manually specify additional harvesting masks for the devices in the cluster.
     * The ones defined by the devices itself have to be used, they will be merged with the ones passed here.
     */
    Cluster(
        const std::set<chip_id_t>& target_devices,
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool skip_driver_allocs = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {});

    /**
     * Cluster constructor.
     * This constructor can be used with custom soc descriptors for the devices on the system.
     *
     * @param sdesc_path SOC descriptor yaml path specifying single chip. The passed soc descriptor will be used as a
     * default device description for devices in the cluster, but each chip will be harvested according to the
     * harvesting info of the devices in the cluster.
     * @param target_devices Devices to target.
     * @param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages).
     * @param skip_driver_allocs
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks Manually specify additional harvesting masks for the devices in the cluster.
     * The ones defined by the devices itself have to be used, they will be merged with the ones passed here.
     */
    Cluster(
        const std::string& sdesc_path,
        const std::set<chip_id_t>& target_devices,
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool skip_driver_allocs = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {});

    /**
     * Cluster constructor.
     * This constructor offers maximal flexibility, allowing the user to pass manually created Chips.
     * The user has to know what they are doing.
     * TODO: Could fail if logical_ids not match the ones in cluster descriptor, while Cluster still uses cluster
     * descriptor.
     *
     * @param chips Map of logical device ids to Chip instances.
     * @param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages).
     * @param skip_driver_allocs
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks
     */
    Cluster(
        std::unordered_map<chip_id_t, std::unique_ptr<Chip>>& chips,
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool skip_driver_allocs = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {});

    /**
     * Cluster constructor which creates a cluster with Mock chips.
     */
    static std::unique_ptr<Cluster> create_mock_cluster();

    // Existing API we want to keep. UMD is transitioning to use CoreCoord instead of tt_xy_pair.
    // This set of function shouldn't be removed even after the transition.
    // TODO: regroup the functions from this set into setup/teardown, runtime, and misc functions.
    virtual void set_barrier_address_params(const barrier_address_params& barrier_address_params_);
    virtual void set_fallback_tlb_ordering_mode(const std::string& fallback_tlb, uint64_t ordering = TLB_DATA::Posted);
    virtual void start_device(const tt_device_params& device_params);
    virtual void assert_risc_reset();
    virtual void deassert_risc_reset();
    virtual void close_device();
    virtual void write_to_sysmem(
        const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void read_from_sysmem(
        void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    virtual void wait_for_non_mmio_flush();
    virtual void wait_for_non_mmio_flush(const chip_id_t chip_id);
    void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels);

    /**
     * Write data to specified address on the BAR space of the device.
     *
     * @param logical_device_id Device to target.
     * @param addr Address to write to.
     * @param data Data to write.
     */
    void bar_write32(int logical_device_id, uint32_t addr, uint32_t data);

    /**
     * Read data from specified address on the BAR space of the device.
     *
     * @param logical_device_id Device to target.
     * @param addr Address to read from.
     */
    uint32_t bar_read32(int logical_device_id, uint32_t addr);

    /**
     * This API allows you to write directly to device memory that is addressable by a static TLB.
     */
    std::function<void(uint32_t, uint32_t, const uint8_t*)> get_fast_pcie_static_tlb_write_callable(int device_id);

    // Misc. Functions to Query/Set Device State
    virtual int arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
    virtual tt_ClusterDescriptor* get_cluster_description();

    /**
     * Get number of MMIO chips detected on the system.
     */
    static int detect_number_of_chips();

    /**
     * Get vector of available MMIO device ids on the system.
     */
    static std::vector<chip_id_t> detect_available_device_ids();

    /**
     * Get set of chip ids for all chips in the cluster.
     */
    virtual std::set<chip_id_t> get_target_device_ids();

    /**
     * Get vector of chip ids for MMIO devices in the cluster.
     */
    virtual std::set<chip_id_t> get_target_mmio_device_ids();

    /**
     * Get vector of chip ids for remote devices in the cluster.
     */
    virtual std::set<chip_id_t> get_target_remote_device_ids();

    virtual std::map<int, int> get_clocks();
    virtual void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;
    virtual std::uint64_t get_pcie_base_addr_from_device(const chip_id_t chip_id) const;
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id);
    virtual std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id);
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id);
    virtual tt_version get_ethernet_fw_version() const;
    // TODO: This should be accessible through public API, probably to be moved to tt_device.

    /**
     * Get PCI device for specified logical device id.
     *
     * @param device_id Device to target.
     */
    PCIDevice* get_pci_device(int device_id) const;

    /**
     * Get TTDevice for specified logical device id.
     *
     * @param device_id Device to target.
     */
    TTDevice* get_tt_device(chip_id_t device_id) const;

    /**
     * Get TLBManager for specified logical device id.
     *
     * @param device_id Device to target.
     */
    TLBManager* get_tlb_manager(chip_id_t device_id) const;

    /**
     * Get Soc descriptor for specified logical device id.
     *
     * @param device_id Device to target.
     */
    const tt_SocDescriptor& get_soc_descriptor(chip_id_t chip_id) const;

    // Existing API we want to remove. UMD is transitioning to use CoreCoord instead of tt_xy_pair.
    // This set of functions is supposed to be removed one the transition for clients (tt-metal, tt-lens) is complete.
    // TODO: remove this set of functions once the transition for clients is completed.
    std::unordered_map<chip_id_t, tt_SocDescriptor> get_virtual_soc_descriptors();
    virtual void configure_tlb(
        chip_id_t logical_device_id,
        tt_xy_pair core,
        int32_t tlb_index,
        uint64_t address,
        uint64_t ordering = TLB_DATA::Posted);
    virtual void configure_active_ethernet_cores_for_mmio_device(
        chip_id_t mmio_chip, const std::unordered_set<tt_xy_pair>& active_eth_cores_per_chip);
    virtual void deassert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET);
    virtual void assert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET);
    virtual void write_to_device(
        const void* mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
    // TODO: Add CoreCoord API for this function.
    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude,
        const std::string& fallback_tlb);
    virtual void read_from_device(
        void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    void l1_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
    void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});

    /**
     * If the tlbs are initialized, returns a tuple with the TLB base address and its size
     */
    std::optional<std::tuple<uint32_t, uint32_t>> get_tlb_data_from_target(const tt_cxy_pair& target);

    /**
     * Returns a struct with the TLB configuration, or throws an exception if the target does not have a static TLB.
     */
    tlb_configuration get_tlb_configuration(const tt_cxy_pair& target);

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
    tt::Writer get_static_tlb_writer(tt_cxy_pair target);
    virtual bool using_harvested_soc_descriptors();
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c);
    static std::vector<int> extract_rows_to_remove(
        const tt::ARCH& arch, const int worker_grid_rows, const int harvested_rows);
    static void remove_worker_row_from_descriptor(
        tt_SocDescriptor& full_soc_descriptor, const std::vector<int>& row_coordinates_to_remove);
    static void harvest_rows_in_soc_descriptor(tt::ARCH arch, tt_SocDescriptor& sdesc, uint32_t harvested_rows);
    static std::unordered_map<tt_xy_pair, tt_xy_pair> create_harvested_coord_translation(
        const tt::ARCH arch, bool identity_map);

    // New API. UMD is transitioning to use CoreCoord instead of tt_xy_pair.
    // This is new set of functions that should be used once the transition for clients (tt-metal, tt-lens) is complete.
    virtual void configure_tlb(
        chip_id_t logical_device_id,
        tt::umd::CoreCoord core,
        int32_t tlb_index,
        uint64_t address,
        uint64_t ordering = TLB_DATA::Posted);
    virtual void deassert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET);
    virtual void assert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET);
    virtual void write_to_device(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        chip_id_t chip,
        tt::umd::CoreCoord core,
        uint64_t addr,
        const std::string& tlb_to_use);
    virtual void read_from_device(
        void* mem_ptr,
        chip_id_t chip,
        tt::umd::CoreCoord core,
        uint64_t addr,
        uint32_t size,
        const std::string& fallback_tlb);
    std::optional<std::tuple<uint32_t, uint32_t>> get_tlb_data_from_target(
        const chip_id_t chip, const tt::umd::CoreCoord core);
    tlb_configuration get_tlb_configuration(const chip_id_t chip, const tt::umd::CoreCoord core);
    tt::Writer get_static_tlb_writer(const chip_id_t chip, const tt::umd::CoreCoord target);
    virtual void configure_active_ethernet_cores_for_mmio_device(
        const std::unordered_set<tt::umd::CoreCoord>& active_eth_cores_per_chip, chip_id_t mmio_chip);
    virtual void l1_membar(
        const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores, const std::string& fallback_tlb);
    virtual void dram_membar(
        const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores, const std::string& fallback_tlb);

    static std::unique_ptr<tt_ClusterDescriptor> create_cluster_descriptor();
    // Destructor
    virtual ~Cluster();

private:
    // Helper functions
    // Startup + teardown
    void create_device(
        const std::set<chip_id_t>& target_mmio_device_ids,
        const uint32_t& num_host_mem_ch_per_mmio_device,
        const bool skip_driver_allocs,
        const bool clean_system_resources);
    void initialize_interprocess_mutexes(int logical_device_id, bool cleanup_mutexes_in_shm);
    void cleanup_shared_host_state();
    void initialize_pcie_devices();
    void broadcast_pcie_tensix_risc_reset(chip_id_t chip_id, const TensixSoftResetOptions& cores);
    void broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions& soft_resets);
    void send_remote_tensix_risc_reset_to_core(const tt_cxy_pair& core, const TensixSoftResetOptions& soft_resets);
    void send_tensix_risc_reset_to_core(const tt_cxy_pair& core, const TensixSoftResetOptions& soft_resets);
    void perform_harvesting_on_soc_descriptors();
    void populate_cores();
    void init_pcie_iatus();  // No more p2p support.
    void check_pcie_device_initialized(int device_id);
    void set_pcie_power_state(tt_DevicePowerState state);
    int set_remote_power_state(const chip_id_t& chip, tt_DevicePowerState device_state);
    void set_power_state(tt_DevicePowerState state);
    uint32_t get_power_state_arc_msg(chip_id_t chip_id, tt_DevicePowerState state);
    void enable_local_ethernet_queue(const chip_id_t& chip, int timeout);
    void enable_ethernet_queue(int timeout);
    void enable_remote_ethernet_queue(const chip_id_t& chip, int timeout);
    void deassert_resets_and_set_power_state();
    int iatu_configure_peer_region(
        int logical_device_id, uint32_t peer_region_id, uint64_t bar_addr_64, uint32_t region_size);
    uint32_t get_harvested_noc_rows(uint32_t harvesting_mask);
    uint32_t get_harvested_rows(int logical_device_id);
    int get_clock(int logical_device_id);

    // Communication Functions
    void read_buffer(
        void* mem_ptr,
        std::uint32_t address,
        std::uint16_t channel,
        std::uint32_t size_in_bytes,
        chip_id_t src_device_id);
    void write_buffer(
        const void* mem_ptr, std::uint32_t size, std::uint32_t address, std::uint16_t channel, chip_id_t src_device_id);
    void write_device_memory(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        tt_cxy_pair target,
        uint64_t address,
        const std::string& fallback_tlb);
    void write_to_non_mmio_device(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        tt_cxy_pair core,
        uint64_t address,
        bool broadcast = false,
        std::vector<int> broadcast_header = {});
    void read_device_memory(
        void* mem_ptr, tt_cxy_pair target, uint64_t address, uint32_t size_in_bytes, const std::string& fallback_tlb);
    void read_from_non_mmio_device(void* mem_ptr, tt_cxy_pair core, uint64_t address, uint32_t size_in_bytes);
    void read_mmio_device_register(
        void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    void write_mmio_device_register(
        const void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    void pcie_broadcast_write(
        chip_id_t chip,
        const void* mem_ptr,
        uint32_t size_in_bytes,
        std::uint32_t addr,
        const tt_xy_pair& start,
        const tt_xy_pair& end,
        const std::string& fallback_tlb);
    void ethernet_broadcast_write(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        const std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& cols_to_exclude,
        const std::string& fallback_tlb,
        bool use_virtual_coords);
    void set_membar_flag(
        const chip_id_t chip,
        const std::unordered_set<tt_xy_pair>& cores,
        const uint32_t barrier_value,
        const uint32_t barrier_addr,
        const std::string& fallback_tlb);
    void insert_host_to_device_barrier(
        const chip_id_t chip,
        const std::unordered_set<tt_xy_pair>& cores,
        const uint32_t barrier_addr,
        const std::string& fallback_tlb);
    void init_membars();
    uint64_t get_sys_addr(
        const tt_driver_noc_params& noc_params,
        uint32_t chip_x,
        uint32_t chip_y,
        uint32_t noc_x,
        uint32_t noc_y,
        uint64_t offset);
    uint16_t get_sys_rack(const tt_driver_eth_interface_params& eth_interface_params, uint32_t rack_x, uint32_t rack_y);
    bool is_non_mmio_cmd_q_full(chip_id_t chip_id, uint32_t curr_wptr, uint32_t curr_rptr);
    int pcie_arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);
    int remote_arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        int timeout = 1,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);

    std::shared_ptr<boost::interprocess::named_mutex> get_mutex(const std::string& tlb_name, int logical_device_id);
    virtual uint32_t get_harvested_noc_rows_for_chip(
        int logical_device_id);  // Returns one-hot encoded harvesting mask for PCIe mapped chips
    void generate_tensix_broadcast_grids_for_grayskull(
        std::set<std::pair<tt_xy_pair, tt_xy_pair>>& broadcast_grids,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& cols_to_exclude);
    std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& get_ethernet_broadcast_headers(
        const std::set<chip_id_t>& chips_to_exclude);
    // Test functions
    void verify_eth_fw();
    void verify_sw_fw_versions(int device_id, std::uint32_t sw_version, std::vector<std::uint32_t>& fw_versions);
    int test_setup_interface();

    // This functions has to be called for local chip, and then it will wait for all connected remote chips to flush.
    void wait_for_connected_non_mmio_flush(chip_id_t chip_id);

    // Helper functions for constructing the chips from the cluster descriptor.
    std::unique_ptr<Chip> construct_chip_from_cluster(
        chip_id_t chip_id, tt_ClusterDescriptor* cluster_desc, tt_SocDescriptor& soc_desc);
    std::unique_ptr<Chip> construct_chip_from_cluster(
        const std::string& soc_desc_path,
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    std::unique_ptr<Chip> construct_chip_from_cluster(
        chip_id_t logical_device_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    void add_chip(chip_id_t chip_id, std::unique_ptr<Chip> chip);
    HarvestingMasks get_harvesting_masks(
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perfrom_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    uint32_t get_tensix_harvesting_mask(
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    // TODO: this function returns only software harvesting mask for DRAM.
    // Combine this with silicon harvesting mask once gathering silicon harvesting mask is implemented.
    uint32_t get_dram_harvesting_mask(
        chip_id_t chip_id,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    // TODO: this function returns only software harvesting mask for ETH.
    // Combine this with silicon harvesting mask once gathering silicon harvesting mask is implemented.
    uint32_t get_eth_harvesting_mask(
        chip_id_t chip_id,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    void construct_cluster(
        const uint32_t& num_host_mem_ch_per_mmio_device,
        const bool skip_driver_allocs,
        const bool clean_system_resources,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks);
    // TODO: These functions should be removed once the transition to CoreCoord is complete.
    CoordSystem get_coord_system_used() const;
    tt_xy_pair translate_to_api_coords(const chip_id_t chip, const tt::umd::CoreCoord core_coord) const;
    // Most of the old APIs accept virtual coordinates, but we communicate with the device through translated
    // coordinates. This is an internal helper function, until we switch the API to accept translated coordinates.
    tt_xy_pair translate_chip_coord_virtual_to_translated(const chip_id_t chip_id, const tt_xy_pair core) const;

    static std::unique_ptr<tt_ClusterDescriptor> create_cluster_descriptor(
        const std::unordered_map<chip_id_t, std::unique_ptr<tt::umd::Chip>>& chips);

    // State variables
    std::vector<tt::ARCH> archs_in_cluster = {};
    std::set<chip_id_t> all_chip_ids_ = {};
    std::set<chip_id_t> remote_chip_ids_ = {};
    std::set<chip_id_t> local_chip_ids_ = {};
    std::unordered_map<chip_id_t, std::unique_ptr<Chip>> chips_;
    tt::ARCH arch_name;

    std::shared_ptr<tt_ClusterDescriptor> cluster_desc;

    // remote eth transfer setup
    static constexpr std::uint32_t NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 6;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 4;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_START_ID = 0;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_MASK = (NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS - 1);

    static constexpr std::uint32_t EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS =
        NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS - NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_START_ID =
        NON_EPOCH_ETH_CORES_START_ID + NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_MASK = (EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS - 1);

    int active_core = NON_EPOCH_ETH_CORES_START_ID;
    std::vector<std::vector<tt_cxy_pair>> remote_transfer_ethernet_cores;
    std::unordered_map<chip_id_t, bool> flush_non_mmio_per_chip = {};
    bool non_mmio_transfer_cores_customized = false;
    std::unordered_map<chip_id_t, int> active_eth_core_idx_per_chip = {};
    std::unordered_map<chip_id_t, bool> noc_translation_enabled_for_chip = {};
    std::map<std::string, std::shared_ptr<boost::interprocess::named_mutex>> hardware_resource_mutex_map = {};
    std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>> harvested_coord_translation = {};
    std::unordered_map<chip_id_t, std::uint32_t> num_rows_harvested = {};
    std::unordered_map<chip_id_t, std::unordered_set<tt_xy_pair>> workers_per_chip = {};
    std::unordered_set<tt_xy_pair> eth_cores = {};
    std::unordered_set<tt_xy_pair> dram_cores = {};

    std::map<std::set<chip_id_t>, std::unordered_map<chip_id_t, std::vector<std::vector<int>>>> bcast_header_cache = {};
    bool perform_harvesting_on_sdesc = false;
    bool use_ethernet_ordered_writes = true;
    bool use_ethernet_broadcast = true;
    bool use_virtual_coords_for_eth_broadcast = true;
    tt_version eth_fw_version;  // Ethernet FW the driver is interfacing with
    // Named Mutexes
    static constexpr char NON_MMIO_MUTEX_NAME[] = "NON_MMIO";
    static constexpr char ARC_MSG_MUTEX_NAME[] = "ARC_MSG";
    static constexpr char MEM_BARRIER_MUTEX_NAME[] = "MEM_BAR";
    // ERISC FW Version Required by UMD
    static constexpr std::uint32_t SW_VERSION = 0x06060000;
};

}  // namespace tt::umd
