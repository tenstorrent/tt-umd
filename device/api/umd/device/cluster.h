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
#include "tt_soc_descriptor.h"
#include "tt_xy_pair.h"
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
    tt_device(){};
    virtual ~tt_device(){};

    // Setup/Teardown Functions
    /**
     * Set Barrier Address Map parameters used by UMD to communicate with the TT Device.
     *
     * @param barrier_address_params_  All the barrier parameters required by UMD
     */
    virtual void set_barrier_address_params(const barrier_address_params& barrier_address_params_) {
        throw std::runtime_error("---- tt_device::set_barrier_address_params is not implemented\n");
    }

    /**
     * Configure a TLB to point to a specific core and an address within that core. Should be done for Static TLBs.
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

    virtual void configure_active_ethernet_cores_for_mmio_device(
        const std::unordered_set<tt::umd::CoreCoord>& active_eth_cores_per_chip, chip_id_t mmio_chip) {
        throw std::runtime_error(
            "---- tt_device::configure_active_ethernet_cores_for_mmio_device is not implemented\n");
    }

    /**
     * On Silicon: Assert soft Tensix reset, deassert RiscV reset, set power state to busy (ramp up AICLK), initialize
     * iATUs for PCIe devices and ethernet queues for remote chips.
     *
     * @param device_params Object specifying initialization configuration.
     */
    virtual void start_device(const tt_device_params& device_params) {
        throw std::runtime_error("---- tt_device::start_device is not implemented\n");
    }

    /**
     * Broadcast deassert soft Tensix Reset to the entire device (to be done after start_device is called).
     */
    virtual void deassert_risc_reset() {
        throw std::runtime_error("---- tt_device::deassert_risc_reset is not implemented\n");
    }

    /**
     * Send a soft deassert reset signal to a single tensix core.
     *
     * @param core Chip and core being targeted.
     */
    virtual void deassert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::deassert_risc_reset_at_core is not implemented\n");
    }

    virtual void deassert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::deassert_risc_reset_at_core is not implemented\n");
    }

    /**
     * Broadcast assert soft Tensix Reset to the entire device.
     */
    virtual void assert_risc_reset() {
        throw std::runtime_error("---- tt_device::assert_risc_reset is not implemented\n");
    }

    /**
     * Send a soft assert reset signal to a single tensix core.
     *
     * @param core Chip and core being targeted.
     */
    virtual void assert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::assert_risc_reset_at_core is not implemented\n");
    }

    virtual void assert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET) {
        throw std::runtime_error("---- tt_device::assert_risc_reset_at_core is not implemented\n");
    }

    /**
     * To be called at the end of a run.
     * Set power state to idle, assert tensix reset at all cores.
     */
    virtual void close_device() { throw std::runtime_error("---- tt_device::close_device is not implemented\n"); }

    // Runtime functions
    /**
     * Non-MMIO (ethernet) barrier.
     * Similar to an mfence for host -> host transfers. Will flush all in-flight ethernet transactions before proceeding
     * with the next one. This will be applied to all chips in the cluster.
     */
    virtual void wait_for_non_mmio_flush() {
        throw std::runtime_error("---- tt_device::wait_for_non_mmio_flush is not implemented\n");
    }

    /**
     * Non-MMIO (ethernet) barrier.
     * This function should be called for a remote chip. If called for local chip, it will be a no-op.
     */
    virtual void wait_for_non_mmio_flush(const chip_id_t chip_id) {
        throw std::runtime_error("---- tt_device::wait_for_non_mmio_flush is not implemented\n");
    }

    /**
     * Write uint32_t data (as specified by ptr + len pair) to specified device, core and address (defined for Silicon).
     *
     * @param mem_ptr Source data address.
     * @param len Source data size.
     * @param core Device and core being targeted.
     * @param addr Address to write to.
     * @param tlb_to_use Specifies fallback/dynamic TLB to use.
     */
    virtual void write_to_device(
        const void* mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

    virtual void write_to_device(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        chip_id_t chip,
        tt::umd::CoreCoord core,
        uint64_t addr,
        const std::string& tlb_to_use) {
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

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
     *
     * @param mem_ptr Data pointer to read the data into.
     * @param core Chip and core being targeted.
     * @param addr Address to read from.
     * @param size Number of bytes to read.
     * @param fallback_tlb Specifies fallback/dynamic TLB to use.
     */
    virtual void read_from_device(
        void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

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
     * Write uint32_t vector to specified address and channel on host (defined for Silicon).
     *
     * @param vec Data to write.
     * @param addr Address to write to.
     * @param channel Host channel to target.
     * @param src_device_id Chip to target.
     */
    virtual void write_to_sysmem(
        const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::write_to_sysmem is not implemented\n");
    }

    virtual void read_from_sysmem(
        void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::read_from_sysmem is not implemented\n");
    }

    virtual void l1_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {}) {
        throw std::runtime_error("---- tt_device::l1_membar is not implemented\n");
    }

    virtual void l1_membar(
        const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores, const std::string& fallback_tlb) {
        throw std::runtime_error("---- tt_device::l1_membar is not implemented\n");
    }

    virtual void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    virtual void dram_membar(
        const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    virtual void dram_membar(
        const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores, const std::string& fallback_tlb) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    // Misc. Functions to Query/Set Device State
    /**
     * Query post harvesting SOC descriptors from UMD in virtual coordinates.
     * These descriptors should be used for looking up cores that are passed into UMD APIs.
     */
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor> get_virtual_soc_descriptors() {
        return soc_descriptor_per_chip;
    }

    /**
     * Get harvesting masks for all chips/SOC Descriptors in the cluster.
     * Each mask represents a map of enabled (0) and disabled (1) rows on a specific chip (in NOC0 Coordinateds).
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
     * Get all logical ids for all MMIO chips targeted by UMD.
     */
    virtual std::set<chip_id_t> get_target_mmio_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_mmio_device_ids is not implemented\n");
    }

    /**
     * Get all logical ids for all Ethernet Mapped chips targeted by UMD.
     */
    virtual std::set<chip_id_t> get_target_remote_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_remote_device_ids is not implemented\n");
    }

    /**
     * Get clock frequencies for all MMIO devices targeted by UMD.
     */
    virtual std::map<int, int> get_clocks() {
        throw std::runtime_error("---- tt_device::get_clocks is not implemented\n");
        return std::map<int, int>();
    }

    virtual std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id) {
        throw std::runtime_error("---- tt_device::get_numa_node_for_pcie_device is not implemented\n");
    }

    /**
     * Get the ethernet firmware version used by the physical cluster (only implemented for Silicon Backend).
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
        return 0;
    }

    /**
     * Get size for a specific DRAM channel on a device.
     *
     * @param device_id Device to target.
     * @param channel DRAM channel to target.
     */
    virtual std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
        throw std::runtime_error("---- tt_device::get_dram_channel_size is not implemented\n");
        return 0;
    }

    /**
     * Query number of Host channels (hugepages) allocated for a specific device.
     *
     * @param device_id Logical device id to target.
     */
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id) {
        throw std::runtime_error("---- tt_device::get_num_host_channels is not implemented\n");
        return 0;
    }

    /**
     * Get size for a specific Host channel accessible by the corresponding device.
     *
     * @param device_id Logical device id to target.
     * @param channel Logical host channel to target.
     */
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
        throw std::runtime_error("---- tt_device::get_host_channel_size is not implemented\n");
        return 0;
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
        return nullptr;
    }

    virtual std::uint64_t get_pcie_base_addr_from_device(const chip_id_t chip_id) const {
        throw std::runtime_error("---- tt_device::get_pcie_base_addr_from_device is not implemented\n");
        return 0;
    }

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
        std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {});

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
        std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {});

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
        std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {});

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
        std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {});

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
    void bar_write32(int logical_device_id, uint32_t addr, uint32_t data);
    uint32_t bar_read32(int logical_device_id, uint32_t addr);
    /**
     * This API allows you to write directly to device memory that is addressable by a static TLB
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
    static int detect_number_of_chips();
    static std::vector<chip_id_t> detect_available_device_ids();
    virtual std::set<chip_id_t> get_target_mmio_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual std::set<chip_id_t> get_target_device_ids();
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
    PCIDevice* get_pci_device(int device_id) const;
    TTDevice* get_tt_device(chip_id_t device_id) const;
    TLBManager* get_tlb_manager(chip_id_t device_id) const;
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
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c);

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
    uint64_t get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint64_t offset);
    uint16_t get_sys_rack(uint32_t rack_x, uint32_t rack_y);
    bool is_non_mmio_cmd_q_full(uint32_t curr_wptr, uint32_t curr_rptr);
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
        std::unordered_map<chip_id_t, uint32_t>& simulated_harvesting_masks);
    std::unique_ptr<Chip> construct_chip_from_cluster(
        chip_id_t logical_device_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, uint32_t>& simulated_harvesting_masks);
    void add_chip(chip_id_t chip_id, std::unique_ptr<Chip> chip);
    uint32_t get_tensix_harvesting_mask(
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, uint32_t>& simulated_harvesting_masks);
    void construct_cluster(
        const uint32_t& num_host_mem_ch_per_mmio_device,
        const bool skip_driver_allocs,
        const bool clean_system_resources,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks);

    // Helper functions for translating chip coordinates.
    tt::umd::CoreCoord translate_chip_coord(
        const chip_id_t chip, const tt::umd::CoreCoord core_coord, const CoordSystem coord_system) const;
    // Most of the old APIs accept virtual coordinates, but we communicate with the device through translate
    // coordinates. This is an internal helper function, until we switch the API to accept translated coordinates.
    tt_xy_pair translate_chip_coord_virtual_to_translated(const chip_id_t chip_id, const tt_xy_pair core) const;

    // State variables
    tt_device_dram_address_params dram_address_params;
    tt_device_l1_address_params l1_address_params;
    tt_driver_host_address_params host_address_params;
    tt_driver_noc_params noc_params;
    tt_driver_eth_interface_params eth_interface_params;
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
