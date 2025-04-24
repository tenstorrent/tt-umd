/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "fmt/core.h"
#include "tt_silicon_driver_common.hpp"
#include "umd/device/chip/chip.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_io.hpp"
#include "umd/device/types/arch.h"
#include "umd/device/types/cluster_descriptor_types.h"
#include "umd/device/types/cluster_types.h"
#include "umd/device/types/tlb.h"

using TLB_DATA = tt::umd::tlb_data;

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
     * Pass in ethernet cores with active links for a specific MMIO chip. When called, this function will force UMD to
     * use a subset of cores from the active_eth_cores_per_chip set for all host->cluster non-MMIO transfers. If this
     * function is not called, UMD will use a default set of ethernet core indices for these transfers (0 through 5). If
     * default behaviour is not desired, this function must be called for all MMIO devices.
     * This API is going to be deprecated when all UMD clients transition to CoreCoord API.
     *
     * @param mmio_chip Device being targeted.
     * @param active_eth_cores_per_chip The active ethernet cores for this chip.
     */
    virtual void configure_active_ethernet_cores_for_mmio_device(
        chip_id_t mmio_chip, const std::unordered_set<tt::umd::CoreCoord>& active_eth_cores_per_chip) {
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
     * @param mem_ptr Source data address.
     * @param size_in_bytes Source data size.
     * @param chip Chip to target.
     * @param core Core to target.
     * @param addr Address to write to.
     */
    virtual void write_to_device(
        const void* mem_ptr, uint32_t size_in_bytes, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) {
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

    /**
     * Write uint32_t data (as specified by ptr + len pair) to specified device, core and address (defined for Silicon).
     * This API is used for writing to both TENSIX and DRAM cores. The internal SocDescriptor can be used to determine
     * which type of the core is being targeted.
     * This API is used for writing to registers in the device address space, reads are slower but are guaranteed to be
     * done when this function returns.
     *
     * @param mem_ptr Source data address.
     * @param size_in_bytes Source data size.
     * @param chip Chip to target.
     * @param core Core to target.
     * @param addr Address to write to.
     */
    virtual void write_to_device_reg(
        const void* mem_ptr, uint32_t size_in_bytes, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) {
        throw std::runtime_error("---- tt_device::write_to_device_reg is not implemented\n");
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
     */
    virtual void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude) {
        throw std::runtime_error("---- tt_device::broadcast_write_to_cluster is not implemented\n");
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
     */
    virtual void read_from_device(
        void* mem_ptr, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr, uint32_t size) {
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

    /**
     * Read uint32_t data from a specified device, core and address to host memory (defined for Silicon).
     * This API is used for reading from both TENSIX and DRAM cores. The internal SocDescriptor can be used to determine
     * which type of the core is being targeted.
     * This API is used for writing to registers in the device address space, reads are slower but are guaranteed to be
     * done when this function returns.
     *
     * @param mem_ptr Data pointer to read the data into.
     * @param chip Chip to target.
     * @param core Core to target.
     * @param addr Address to read from.
     * @param size Number of bytes to read.
     */
    virtual void read_from_device_reg(
        void* mem_ptr, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr, uint32_t size) {
        throw std::runtime_error("---- tt_device::read_from_device_reg is not implemented\n");
    }

    /**
     * Use PCIe DMA to write device memory (L1 or DRAM).
     *
     * @param src Source data address.
     * @param size Size in bytes.
     * @param chip Chip to target; must be local, i.e. attached via PCIe.
     * @param core Core to target.
     * @param addr Address to write to.
     */
    virtual void dma_write_to_device(
        const void* src, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) = 0;

    /**
     * Use PCIe DMA to read device memory (L1 or DRAM).
     *
     * @param src Source data address.
     * @param size Size in bytes.
     * @param chip Chip to target; must be local, i.e. attached via PCIe.
     * @param core Core to target.
     * @param addr Address to read from.
     */
    virtual void dma_read_from_device(
        void* dst, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) = 0;

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
     * @param chip Chip to target.
     * @param cores Cores being targeted.
     */
    virtual void l1_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores = {}) {
        throw std::runtime_error("---- tt_device::l1_membar is not implemented\n");
    }

    /**
     * DRAM memory barrier.
     * This should be called when the client wants to ensure that all transactions on the specified dram bank have
     * completed.
     *
     * @param chip Chip to target.
     * @param channels Channels being targeted.
     */
    virtual void dram_membar(const chip_id_t chip, const std::unordered_set<uint32_t>& channels = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    /**
     * DRAM memory barrier.
     * This should be called when the client wants to ensure that all transactions on the specified dram bank have
     * completed.
     *
     * @param chip Chip being targeted.
     * @param cores Cores being targeted.
     */
    virtual void dram_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    /**
     * Issue message to device, meant to be picked up by ARC firmware.
     *
     * @param logical_device_id Chip to target.
     * @param msg_code Specifies type of ARC message.
     * @param wait_for_done Block until ARC responds.
     * @param arg0 Message related argument.
     * @param arg1 Message related argument.
     * @param timeout_ms Timeout in milliseconds.
     * @param return3 Return value from ARC.
     * @param return4 Return value from ARC.
     */
    virtual int arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        uint32_t timeout_ms = 1000,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) {
        throw std::runtime_error("---- tt_device::arc_msg is not implemented\n");
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
        throw std::runtime_error("---- tt_device::get_soc_descriptor is not implemented\n");
    }
};

namespace tt::umd {

class LocalChip;
class RemoteChip;

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
     * @param create_mock_chips Create mock chips for the devices in the cluster descriptor.
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks Manually specify additional harvesting masks for the devices in the cluster.
     * The ones defined by the devices itself have to be used, they will be merged with the ones passed here.
     */
    Cluster(
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool create_mock_chips = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {});

    /**
     * Cluster constructor.
     * This constructor can be used to target only specific devices on the system.
     *
     * @param target_devices Devices to target.
     * @param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages).
     * @param create_mock_chips Create mock chips for the devices in the cluster descriptor.
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks Manually specify additional harvesting masks for the devices in the cluster.
     * The ones defined by the devices itself have to be used, they will be merged with the ones passed here.
     */
    Cluster(
        const std::set<chip_id_t>& target_devices,
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool create_mock_chips = false,
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
     * @param create_mock_chips Create mock chips for the devices in the cluster descriptor.
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks Manually specify additional harvesting masks for the devices in the cluster.
     * The ones defined by the devices itself have to be used, they will be merged with the ones passed here.
     */
    Cluster(
        const std::string& sdesc_path,
        const std::set<chip_id_t>& target_devices,
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool create_mock_chips = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {});

    /**
     * Cluster constructor.
     * This constructor can be used with custom cluster descriptor. If the cluster descriptor does not match the
     * actual devices on the system, the constructor will throw an exception. If create_mock_chips is set to true,
     * the constructor will create mock chips for the devices in the cluster descriptor.
     *
     * @param cluster_descriptor Cluster descriptor object based on which Cluster is going to be created.
     * @param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages).
     * @param create_mock_chips Create mock chips for the devices in the cluster descriptor.
     * @param clean_system_resource Specifies if host state from previous runs needs to be cleaned up.
     * @param perform_harvesting Allow the driver to modify the SOC descriptors per chip.
     * @param simulated_harvesting_masks Manually specify additional harvesting masks for the devices in the cluster.
     * The ones defined by the devices itself have to be used, they will be merged with the ones passed here.
     */
    Cluster(
        std::unique_ptr<tt_ClusterDescriptor> cluster_descriptor,
        const uint32_t& num_host_mem_ch_per_mmio_device = 1,
        const bool create_mock_chips = false,
        const bool clean_system_resources = false,
        bool perform_harvesting = true,
        std::unordered_map<chip_id_t, HarvestingMasks> simulated_harvesting_masks = {});

    // Existing API we want to keep. UMD is transitioning to use CoreCoord instead of tt_xy_pair.
    // This set of function shouldn't be removed even after the transition.
    // TODO: regroup the functions from this set into setup/teardown, runtime, and misc functions.
    virtual void set_barrier_address_params(const barrier_address_params& barrier_address_params_);
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
        uint32_t timeout_ms = 1000,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr);
    virtual tt_ClusterDescriptor* get_cluster_description();

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
     * Get Chip for specified logical device id.
     *
     * @param device_id Device to target.
     */
    Chip* get_chip(chip_id_t device_id) const;

    /**
     * Get Chip for specified logical device id, verify it is local.
     *
     * @param device_id Device to target.
     */
    LocalChip* get_local_chip(chip_id_t device_id) const;

    /**
     * Get Chip for specified logical device id, verify it is remote.
     *
     * @param device_id Device to target.
     */
    RemoteChip* get_remote_chip(chip_id_t device_id) const;

    /**
     * Get Soc descriptor for specified logical device id.
     *
     * @param device_id Device to target.
     */
    const tt_SocDescriptor& get_soc_descriptor(chip_id_t chip_id) const;

    // Existing API we want to remove. UMD is transitioning to use CoreCoord instead of tt_xy_pair.
    // This set of functions is supposed to be removed one the transition for clients (tt-metal, tt-lens) is complete.
    // TODO: remove this set of functions once the transition for clients is completed.
    virtual void configure_tlb(
        chip_id_t logical_device_id,
        tt_xy_pair core,
        int32_t tlb_index,
        uint64_t address,
        uint64_t ordering = TLB_DATA::Posted);
    virtual void deassert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET);
    virtual void assert_risc_reset_at_core(
        tt_cxy_pair core, const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET);
    // TODO: Add CoreCoord API for this function.
    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude);

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
        const void* mem_ptr, uint32_t size_in_bytes, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr);
    virtual void write_to_device_reg(
        const void* mem_ptr, uint32_t size_in_bytes, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr);
    virtual void read_from_device(void* mem_ptr, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr, uint32_t size);
    virtual void read_from_device_reg(
        void* mem_ptr, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr, uint32_t size);
    virtual void dma_write_to_device(
        const void* src, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr);
    virtual void dma_read_from_device(void* dst, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr);
    std::optional<std::tuple<uint32_t, uint32_t>> get_tlb_data_from_target(
        const chip_id_t chip, const tt::umd::CoreCoord core);
    tlb_configuration get_tlb_configuration(const chip_id_t chip, const tt::umd::CoreCoord core);
    tt::Writer get_static_tlb_writer(const chip_id_t chip, const tt::umd::CoreCoord target);
    virtual void configure_active_ethernet_cores_for_mmio_device(
        chip_id_t mmio_chip, const std::unordered_set<CoreCoord>& active_eth_cores_per_chip);
    void l1_membar(const chip_id_t chip, const std::unordered_set<CoreCoord>& cores = {});
    void dram_membar(const chip_id_t chip, const std::unordered_set<CoreCoord>& cores = {});
    void dram_membar(const chip_id_t chip, const std::unordered_set<uint32_t>& channels);
    void set_power_state(tt_DevicePowerState state);

    static std::unique_ptr<tt_ClusterDescriptor> create_cluster_descriptor(std::string sdesc_path = "");

    static std::string serialize();

    static std::filesystem::path serialize_to_file(const std::filesystem::path& dest_file = "");

    // Destructor
    virtual ~Cluster();

private:
    // Helper functions
    // Startup + teardown
    void create_device(
        const std::set<chip_id_t>& target_mmio_device_ids,
        const uint32_t& num_host_mem_ch_per_mmio_device,
        const bool create_mock_chips);
    void broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions& soft_resets);
    void send_remote_tensix_risc_reset_to_core(const tt_cxy_pair& core, const TensixSoftResetOptions& soft_resets);
    void send_tensix_risc_reset_to_core(const tt_cxy_pair& core, const TensixSoftResetOptions& soft_resets);
    void set_pcie_power_state(tt_DevicePowerState state);
    int set_remote_power_state(const chip_id_t& chip, tt_DevicePowerState device_state);
    uint32_t get_power_state_arc_msg(chip_id_t chip_id, tt_DevicePowerState state);
    void enable_ethernet_queue(int timeout);
    void deassert_resets_and_set_power_state();
    int get_clock(int logical_device_id);
    void wait_for_aiclk_value(tt_DevicePowerState power_state, const uint32_t timeout_ms = 5000);

    // Communication Functions
    void ethernet_broadcast_write(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        const std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& cols_to_exclude,
        bool use_virtual_coords);

    std::unordered_map<chip_id_t, std::vector<std::vector<int>>>& get_ethernet_broadcast_headers(
        const std::set<chip_id_t>& chips_to_exclude);
    // Test functions
    void verify_eth_fw();
    void verify_sw_fw_versions(int device_id, std::uint32_t sw_version, std::vector<std::uint32_t>& fw_versions);

    // Helper functions for constructing the chips from the cluster descriptor.
    std::unique_ptr<Chip> construct_chip_from_cluster(
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        tt_SocDescriptor& soc_desc,
        int num_host_mem_channels,
        const bool clean_system_resources,
        const bool create_mock_chip = false);
    std::unique_ptr<Chip> construct_chip_from_cluster(
        const std::string& soc_desc_path,
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks,
        int num_host_mem_channels,
        const bool clean_system_resources,
        const bool create_mock_chip = false);
    std::unique_ptr<Chip> construct_chip_from_cluster(
        chip_id_t logical_device_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks,
        int num_host_mem_channels,
        const bool clean_system_resources,
        const bool create_mock_chip = false);
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
    uint32_t get_dram_harvesting_mask(
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    uint32_t get_eth_harvesting_mask(
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    uint32_t get_pcie_harvesting_mask(
        chip_id_t chip_id,
        tt_ClusterDescriptor* cluster_desc,
        bool perform_harvesting,
        std::unordered_map<chip_id_t, HarvestingMasks>& simulated_harvesting_masks);
    void construct_cluster(const uint32_t& num_host_mem_ch_per_mmio_device, const bool create_mock_chips);
    tt_xy_pair translate_to_api_coords(const chip_id_t chip, const tt::umd::CoreCoord core_coord) const;
    // Most of the old APIs accept virtual coordinates, but we communicate with the device through translated
    // coordinates. This is an internal helper function, until we switch the API to accept translated coordinates.
    tt_xy_pair translate_chip_coord_virtual_to_translated(const chip_id_t chip_id, const tt_xy_pair core) const;

    static std::unique_ptr<tt_ClusterDescriptor> create_cluster_descriptor(
        const std::unordered_map<chip_id_t, std::unique_ptr<tt::umd::Chip>>& chips);

    static void ubb_eth_connections(
        const std::unordered_map<chip_id_t, std::unique_ptr<tt::umd::Chip>>& chips,
        std::unique_ptr<tt_ClusterDescriptor>& cluster_desc);

    // State variables
    std::set<chip_id_t> all_chip_ids_ = {};
    std::set<chip_id_t> remote_chip_ids_ = {};
    std::set<chip_id_t> local_chip_ids_ = {};
    std::unordered_map<chip_id_t, std::unique_ptr<Chip>> chips_;
    tt::ARCH arch_name;

    std::shared_ptr<tt_ClusterDescriptor> cluster_desc;

    std::map<std::set<chip_id_t>, std::unordered_map<chip_id_t, std::vector<std::vector<int>>>> bcast_header_cache = {};
    bool use_ethernet_broadcast = true;
    bool use_virtual_coords_for_eth_broadcast = true;
    tt_version eth_fw_version;  // Ethernet FW the driver is interfacing with
    // ERISC FW Version Required by UMD
    static constexpr std::uint32_t SW_VERSION = 0x06060000;
};

}  // namespace tt::umd
