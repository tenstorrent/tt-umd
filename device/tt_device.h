/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <set>

#include "tt_soc_descriptor.h"
#include "tt_xy_pair.h"
#include "tt_silicon_driver_common.hpp"
#include "device/tt_cluster_descriptor_types.h"

namespace boost::interprocess{
    class named_mutex;
}

class PCIDevice;
class tt_ClusterDescriptor;

enum tt_DevicePowerState {
    BUSY,
    SHORT_IDLE,
    LONG_IDLE
};

enum tt_MutexType {
    LARGE_READ_TLB,
    LARGE_WRITE_TLB,
    SMALL_READ_WRITE_TLB,
    ARC_MSG
};

enum tt_MemBarFlag {
    SET = 0xaa,
    RESET = 0xbb,
};

inline std::ostream &operator <<(std::ostream &os, const tt_DevicePowerState power_state) {
    switch (power_state) {
        case tt_DevicePowerState::BUSY: os << "Busy"; break;
        case tt_DevicePowerState::SHORT_IDLE: os << "SHORT_IDLE"; break;
        case tt_DevicePowerState::LONG_IDLE: os << "LONG_IDLE"; break;
        default: throw ("Unknown DevicePowerState");
    }
    return os;
}

struct tt_device_dram_address_params {
    std::int32_t DRAM_BARRIER_BASE = 0;
};
/**
 * @brief Struct encapsulating all L1 Address Map parameters required by UMD.
 * These parameters are passed to the constructor.
*/
struct tt_device_l1_address_params {
    std::int32_t NCRISC_FW_BASE = 0;
    std::int32_t FW_BASE = 0;
    std::int32_t TRISC0_SIZE = 0;
    std::int32_t TRISC1_SIZE = 0;
    std::int32_t TRISC2_SIZE = 0;
    std::int32_t TRISC_BASE = 0;
    std::int32_t TENSIX_L1_BARRIER_BASE = 0;
    std::int32_t ETH_L1_BARRIER_BASE = 0;
    std::int32_t FW_VERSION_ADDR = 0;
};

/**
 * @brief Struct encapsulating all Host Address Map parameters required by UMD.
 * These parameters are passed to the constructor and are needed for non-MMIO transactions.
*/
struct tt_driver_host_address_params {
    std::int32_t ETH_ROUTING_BLOCK_SIZE = 0;
    std::int32_t ETH_ROUTING_BUFFERS_START = 0;
};

/**
 * @brief Struct encapsulating all ERISC Firmware parameters required by UMD.
 * These parameters are passed to the constructor and are needed for non-MMIO transactions.
*/
struct tt_driver_eth_interface_params {
    std::int32_t NOC_ADDR_LOCAL_BITS = 0;
    std::int32_t NOC_ADDR_NODE_ID_BITS = 0;
    std::int32_t ETH_RACK_COORD_WIDTH = 0;
    std::int32_t CMD_BUF_SIZE_MASK = 0;
    std::int32_t MAX_BLOCK_SIZE = 0;
    std::int32_t REQUEST_CMD_QUEUE_BASE = 0;
    std::int32_t RESPONSE_CMD_QUEUE_BASE = 0;
    std::int32_t CMD_COUNTERS_SIZE_BYTES = 0;
    std::int32_t REMOTE_UPDATE_PTR_SIZE_BYTES = 0;
    std::int32_t CMD_DATA_BLOCK = 0;
    std::int32_t CMD_WR_REQ = 0;
    std::int32_t CMD_WR_ACK = 0;
    std::int32_t CMD_RD_REQ = 0;
    std::int32_t CMD_RD_DATA = 0;
    std::int32_t CMD_BUF_SIZE = 0;
    std::int32_t CMD_DATA_BLOCK_DRAM = 0;
    std::int32_t ETH_ROUTING_DATA_BUFFER_ADDR = 0;
    std::int32_t REQUEST_ROUTING_CMD_QUEUE_BASE = 0;
    std::int32_t RESPONSE_ROUTING_CMD_QUEUE_BASE = 0;
    std::int32_t CMD_BUF_PTR_MASK = 0;
    std::int32_t CMD_ORDERED = 0;
    std::int32_t CMD_BROADCAST = 0;
};

struct tt_version {
    std::uint16_t major = 0xffff;
    std::uint8_t minor = 0xff;
    std::uint8_t patch = 0xff;
    tt_version() {}
    tt_version(std::uint16_t major_, std::uint8_t minor_, std::uint8_t patch_) {
        major = major_;
        minor = minor_;
        patch = patch_;
    }
    tt_version(std::uint32_t version) {
        major = (version >> 16) & 0xff;
        minor = (version >> 12) & 0xf;
        patch = version & 0xfff;
    }
    std::string str() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }
};

struct tt_device_params {
    bool register_monitor = false;
    bool enable_perf_scoreboard = false;
    std::vector<std::string> vcd_dump_cores;
    std::vector<std::string> plusargs;
    bool init_device = true;
    bool early_open_device = false;
    int aiclk = 0;
    // The command-line input for vcd_dump_cores can have the following format:
    // {"*-2", "1-*", "*-*", "1-2"}
    // '*' indicates we must dump all the cores in that dimension.
    // This function takes the vector above and unrolles the coords with '*' in one or both dimensions.
    std::vector<std::string> unroll_vcd_dump_cores(tt_xy_pair grid_size) const {
        std::vector<std::string> unrolled_dump_core;
        for (auto &dump_core: vcd_dump_cores) {
            // If the input is a single *, then dump all cores.
            if (dump_core == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                for (size_t y = 0; y < grid_size.y; y++) {
                    std::string current_core_coord(std::to_string(x) + "-" + std::to_string(y));
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                        unrolled_dump_core.push_back(current_core_coord);
                    }
                }
                }
                continue;
            }
            // Each core coordinate must contain three characters: "core.x-core.y".
            assert(dump_core.size() <= 5);
            size_t delimiter_pos = dump_core.find('-');
            assert (delimiter_pos != std::string::npos); // y-dim should exist in core coord.

            std::string core_dim_x = dump_core.substr(0, delimiter_pos);
            size_t core_dim_y_start = delimiter_pos + 1;
            std::string core_dim_y = dump_core.substr(core_dim_y_start, dump_core.length() - core_dim_y_start);

            if (core_dim_x == "*" && core_dim_y == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                    for (size_t y = 0; y < grid_size.y; y++) {
                        std::string current_core_coord(std::to_string(x) + "-" + std::to_string(y));
                        if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                            unrolled_dump_core.push_back(current_core_coord);
                        }
                    }
                }
            } else if (core_dim_x == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                    std::string current_core_coord(std::to_string(x) + "-" + core_dim_y);
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                        unrolled_dump_core.push_back(current_core_coord);
                    }
                }
            } else if (core_dim_y == "*") {
                for (size_t y = 0; y < grid_size.y; y++) {
                    std::string current_core_coord(core_dim_x + "-" + std::to_string(y));
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) == std::end(unrolled_dump_core)) {
                        unrolled_dump_core.push_back(current_core_coord);
                    }
                }
            } else {
                unrolled_dump_core.push_back(dump_core);
            }
        }
        return unrolled_dump_core;
    }

    std::vector<std::string> expand_plusargs() const {
        std::vector<std::string> all_plusargs {
            "+enable_perf_scoreboard=" + std::to_string(enable_perf_scoreboard),
            "+register_monitor=" + std::to_string(register_monitor)
        };

        all_plusargs.insert(all_plusargs.end(), plusargs.begin(), plusargs.end());

        return all_plusargs;
    }
};

/**
 * @brief Parent class for tt_SiliconDevice (Silicon Driver) and tt_VersimDevice (Versim Backend API).
 * Exposes a generic interface to callers, providing declarations for virtual functions defined differently for
 * Silicon and Versim.
 * Valid usage consists of declaring a tt_device object and initializing it to either a Silicon or Versim backend.
 * Using tt_device itself will throw errors, since its APIs are undefined.
 */ 
class tt_device
{
    public:
    tt_device(const std::string& sdesc_path);
    virtual ~tt_device();
    // Setup/Teardown Functions
    /**
    * @brief Set L1 Address Map parameters used by UMD to communicate with the TT Device
    * \param l1_address_params_ tt_device_l1_address_params encapsulating all the L1 parameters required by UMD
    */ 
    virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
        throw std::runtime_error("---- tt_device::set_device_l1_address_params is not implemented\n");
    }

    virtual void set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {
        throw std::runtime_error("---- tt_device::set_device_dram_address_params is not implemented\n");
    }

    /**
    * @brief Set Host Address Map parameters used by UMD to communicate with the TT Device (used for remote transactions)
    * \param host_address_params_ tt_driver_host_address_params encapsulating all the Host Address space parameters required by UMD
    */ 

    virtual void set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_) {
        throw std::runtime_error("---- tt_device::set_driver_host_address_params is not implemented\n");
    }

    /**
    * @brief Set ERISC Firmware parameters used by UMD to communicate with the TT Device (used for remote transactions)
    * \param eth_interface_params_ tt_driver_eth_interface_params encapsulating all the Ethernet Firmware parameters required by UMD
    */ 
    virtual void set_driver_eth_interface_params(const tt_driver_eth_interface_params& eth_interface_params_) {
        throw std::runtime_error("---- tt_device::set_driver_eth_interface_params is not implemented\n");
    }

    /**
    * @brief Configure a TLB to point to a specific core and an address within that core. Should be done for Static TLBs
    * \param logical_device_id Logical Device being targeted
    * \param core The TLB will be programmed to point to this core
    * \param tlb_index TLB id that will be programmed
    * \param address All incoming transactions to the TLB will be routed to an address space starting at this parameter (after its aligned to the TLB size)
    * \param ordering Ordering mode for the TLB. Can be Strict (ordered and blocking, since this waits for ack -> slow), Relaxed (ordered, but non blocking -> fast) or Posted (no ordering, non blocking -> fastest).
    */ 
    virtual void configure_tlb(chip_id_t logical_device_id, tt_xy_pair core, std::int32_t tlb_index, std::int32_t address, uint64_t ordering = TLB_DATA::Relaxed) {
        throw std::runtime_error("---- tt_device::configure_tlb is not implemented\n");
    }

    /**
    * @brief Set ordering mode for dynamic/fallback TLBs (passed into driver constructor)
    * \param fallback_tlb Dynamic TLB being targeted
    * \param ordering Ordering mode for the TLB. Can be Strict (ordered and blocking, since this waits for ack -> slow), Posted (ordered, but non blocking -> fast) or Relaxed (no ordering, non blocking -> fastest).
    */ 
    virtual void set_fallback_tlb_ordering_mode(const std::string& fallback_tlb, uint64_t ordering = TLB_DATA::Posted) {
        throw std::runtime_error("---- tt_device::set_fallback_tlb_ordering_mode is not implemented\n");
    }
    /**
    * @brief Give UMD a 1:1 function mapping a core to its appropriate static TLB (currently only support a single TLB per core).
    * \param mapping_function An std::function object with tt_xy_pair as an input, returning the int32_t TLB index for the input core. If the core does not have a mapped TLB, the function should return -1.
    */ 
    virtual void setup_core_to_tlb_map(std::function<std::int32_t(tt_xy_pair)> mapping_function) {
        throw std::runtime_error("---- tt_device::setup_core_to_tlb_map is not implemented\n");
    }
    /** 
     * @brief Start the Silicon on Versim Device
     * On Silicon: Assert soft Tensix reset, deassert RiscV reset, set power state to busy (ramp up AICLK), initialize iATUs for PCIe devices and ethernet queues for remote chips.
     * \param device_params tt_device_params object specifying initialization configuration
    */
    virtual void start_device(const tt_device_params &device_params) {
        throw std::runtime_error("---- tt_device::start_device is not implemented\n");
    }
    /** 
     * @brief Broadcast deassert soft Tensix Reset to the entire device (to be done after start_device is called)
     * \param target_device Logical device id being targeted
    */  
    virtual void deassert_risc_reset() {
        throw std::runtime_error("---- tt_device::deassert_risc_reset is not implemented\n");
    }
    /** 
     * @brief Send a soft deassert reset signal to a single tensix core 
     * \param core tt_cxy_pair specifying the chip and core being targeted
    */  
    virtual void deassert_risc_reset_at_core(tt_cxy_pair core) {
        throw std::runtime_error("---- tt_device::deassert_risc_reset_at_core is not implemented\n");
    }

    /** 
     * @brief Broadcast assert soft Tensix Reset to the entire device
     * \param target_device Logical device id being targeted
    */  
    virtual void assert_risc_reset() {
        throw std::runtime_error("---- tt_device::assert_risc_reset is not implemented\n");
    }
    /** 
     * @brief Send a soft assert reset signal to a single tensix core 
     * \param core tt_cxy_pair specifying the chip and core being targeted
    */  
    virtual void assert_risc_reset_at_core(tt_cxy_pair core) {
        throw std::runtime_error("---- tt_device::assert_risc_reset_at_core is not implemented\n");
    }
    /** 
     * @brief To be called at the end of a run.
     * Set power state to idle, assert tensix reset at all cores.
    */  
    virtual void close_device() {
        throw std::runtime_error("---- tt_device::close_device is not implemented\n");
    }

    // Runtime functions
    /**
     * @brief Non-MMIO (ethernet) barrier.
     * Similar to an mfence for host -> host transfers. Will flush all in-flight ethernet transactions before proceeding with the next one.
    */ 
    virtual void wait_for_non_mmio_flush() {
        throw std::runtime_error("---- tt_device::wait_for_non_mmio_flush is not implemented\n");
    }
    /**
    * @brief Write uint32_t data (as specified by ptr + len pair) to specified device, core and address (defined for Silicon).
    * \param mem_ptr src data address
    * \param len src data size (specified for uint32_t)
    * \param core chip-x-y struct specifying device and core
    * \param addr Address to write to
    * \param tlb_to_use Specifies fallback/dynamic TLB to use for transaction, if this core does not have static TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
    * \param send_epoch_cmd Specifies that this is an epoch_cmd write, forcing runtime to take a faster write path (Buda only)
    * \param last_send_epoch_cmd Specifies that this is the last epoch command being written, which requires metadata to be updated (Buda only)
    */
    virtual void write_to_device(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }
    virtual void broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude,  std::set<uint32_t>& rows_to_exclude,  std::set<uint32_t>& columns_to_exclude, const std::string& fallback_tlb) {
        throw std::runtime_error("---- tt_device::broadcast_write_to_cluster is not implemented\n");
    }
    /**
    * @brief Write uint32_t vector to specified device, core and address (defined for Silicon and Versim).
    * \param vec Vector to write
    * \param core chip-x-y struct specifying device and core
    * \param addr Address to write to
    * \param tlb_to_use Specifies fallback/dynamic TLB to use for transaction, if this core does not have static TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
    * \param send_epoch_cmd Specifies that this is an epoch_cmd write, forcing runtime to take a faster write path (Buda only)
    * \param last_send_epoch_cmd Specifies that this is the last epoch command being written, which requires metadata to be updated (Buda only)
    */
    virtual void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false) {
        throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
    }

    /**
    * @brief Unroll/replicate uint32_t data (as specified by ptr + len pair) and write it to specified device, core and address (defined for Silicon).
    * \param mem_ptr src data address
    * \param len src data size (specified for uint32_t)
    * \param unroll_count Number of times vector should be unrolled
    * \param core chip-x-y struct specifying device and core
    * \param addr Address to write to
    * \param fallback_tlb Specifies fallback/dynamic TLB to use for transaction, if this core does not have static TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
    */
    virtual void rolled_write_to_device(uint32_t* mem_ptr, uint32_t size_in_bytes, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::rolled_write_to_device is not implemented\n");
    }
    /**
    * @brief Unroll/replicate a uint32_t vector and write it to specified device, core and address (defined for Silicon and Versim).
    * \param vec Vector to write
    * \param unroll_count Number of times vector should be unrolled
    * \param core chip-x-y struct specifying device and core
    * \param addr Address to write to
    * \param tlb_to_use Specifies fallback/dynamic TLB to use for transaction, if this core does not have static TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
    */
    virtual void rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
        throw std::runtime_error("---- tt_device::rolled_write_to_device is not implemented\n");
    }

    /**
    * @brief Read uint32_t data from a specified device, core and address to host memory (defined for Silicon).
    * \param mem_ptr dest data address on host (expected to be preallocated, depending on transfer size)
    * \param core chip-x-y struct specifying device and core
    * \param addr Address to read from
    * \param size Read Size
    * \param fallback_tlb Specifies fallback/dynamic TLB to use for transaction, if this core does not have static TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
    */
    virtual void read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
        // Only implement this for Silicon Backend
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

    /**
    * @brief Read a uint32_t vector from a specified device, core and address to host memory (defined for Silicon and Versim).
    * \param vec host side vector to populate with data read from device (does not need to be preallocated)
    * \param core chip-x-y struct specifying device and core
    * \param addr Address to read from
    * \param size Read Size
    * \param fallback_tlb Specifies fallback/dynamic TLB to use for transaction, if this core does not have static TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
    */
    virtual void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {
        throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
    }

    /**
    * @brief Write uint32_t vector to specified address and channel on host (defined for Silicon).
    * \param vec Vector to write
    * \param addr Address to write to
    * \param channel Host channel to target (each MMIO Mapped chip has its own set of channels)
    * \param src_device_id Chip level specifier identifying which chip's host address space needs to be targeted
    */
    virtual void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::write_to_sysmem is not implemented\n");
    }

    virtual void write_to_sysmem(const void* mem_ptr, std::uint32_t size,  uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::write_to_sysmem is not implemented\n");
    }
    /**
    * @brief Read uint32_t vector from specified address and channel on host (defined for Silicon).
    * \param vec Vector to read (does not need to be preallocated)
    * \param addr Address to read from
    * \param channel Host channel to query (each MMIO Mapped chip has its own set of channels)
    * \param src_device_id Chip level specifier identifying which chip's host address space needs to be queried
    */
    virtual void read_from_sysmem(std::vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::read_from_sysmem is not implemented\n");
    }
    virtual void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
        throw std::runtime_error("---- tt_device::read_from_sysmem is not implemented\n");
    }
    virtual void l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {}) {
        throw std::runtime_error("---- tt_device::l1_membar is not implemented\n");
    }
    virtual void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }
    virtual void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {}) {
        throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
    }

    // Misc. Functions to Query/Set Device State
    /**
    * @brief Query post harvesting SOC descriptors from UMD in virtual coordinates. 
    * These descriptors should be used for looking up cores that are passed into UMD APIs.
    * \returns A map of SOC Descriptors per chip.
    */
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors() {
        throw std::runtime_error("---- tt_device:get_virtual_soc_descriptors is not implemented\n");
    }
   
    /**
    * @brief Determine if UMD performed harvesting on SOC descriptors.
    * \returns true if the cluster contains harvested chips and if perform_harvesting was set to 
    * true in the driver constructor
    */
    virtual bool using_harvested_soc_descriptors() {
        throw std::runtime_error("---- tt_device:using_harvested_soc_descriptors is not implemented\n");
        return 0;
    }
    
    /**
     * @brief Get harvesting masks for all chips/SOC Descriptors in the cluster
     * \returns A map of one hot encoded masks showing the physical harvesting state per chip.
     * Each mask represents a map of enabled (0) and disabled (1) rows on a specific chip (in NOC0 Coordinateds).
     */ 
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors() {
        throw std::runtime_error("---- tt_device:get_harvesting_masks_for_soc_descriptors is not implemented\n");
    }
     /**
     * @brief Get Hardware Translation Table state
     * \returns true if translation tables are enabled (WH only)
     */ 
    virtual bool noc_translation_en() {
        throw std::runtime_error("---- tt_device:noc_translation_en is not implemented\n");
    }
    /**
     * @brief Issue message to device, meant to be picked up by ARC Firmare
     * \param logical_device_id Chip to target
     * \param msg_code Specifies type of message (understood by ARC FW)
     * \param wait_for_done Block until ARC responds
     * \param arg0 Message related argument understood by ARC
     * \param arg1 Message related argument understood by ARC
     * \param timeout Timeout on ARC
     * \param return3 Return value from ARC
     * \param return4 Return value from ARC
     * \returns Exit code based on ARC status after the message was issued
     */ 
    virtual int arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr) {
        throw std::runtime_error("---- tt_device::arc_msg is not implemented\n");
    }
    /**
     * @brief Translate between virtual coordinates (from UMD SOC Descriptor) and Translated Coordinates
     * \param device_id Logical Device for which the core coordinates need to be transalted
     * \param r Row coordinate
     * \param c Column coordinate
     */ 
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {
        throw std::runtime_error("---- tt_device::translate_to_noc_table_coords is not implemented\n");
    }
    /**
     * @brief Get the total number of chips in the cluster based on the network descriptor
     * \returns Total number of chips
     */ 
    virtual int get_number_of_chips_in_cluster() {
        throw std::runtime_error("---- tt_device::get_number_of_chips_in_cluster is not implemented\n");
    }
    /**
     * @brief Get the logical ids for all chips in the cluster
     * \returns Unordered set with logical chip ids
     */ 
    virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster() {
        throw std::runtime_error("---- tt_device::get_all_chips_in_cluster is not implemented\n");
    }
    /**
     * @brief Get cluster descriptor object being used in UMD instance
     * \returns Pointer to cluster descriptor object from UMD
     */ 
    virtual tt_ClusterDescriptor* get_cluster_description() {
        throw std::runtime_error("---- tt_device::get_cluster_description is not implemented\n");
    }
    /**
     * @brief Get all logical ids for all MMIO chips targeted by UMD
     * \returns Set with logical ids for MMIO chips 
    */
    virtual std::set<chip_id_t> get_target_mmio_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_mmio_device_ids is not implemented\n");
    }
    /**
     * @brief Get all logical ids for all Ethernet Mapped chips targeted by UMD
     * \returns Set with logical ids for Ethernet Mapped chips 
    */
    virtual std::set<chip_id_t> get_target_remote_device_ids() {
        throw std::runtime_error("---- tt_device::get_target_remote_device_ids is not implemented\n");
    }
    /**
     * @brief Get clock frequencies for all MMIO devices targeted by UMD
     * \returns Map of logical chip id to clock frequency (for MMIO chips only)
    */
    virtual std::map<int,int> get_clocks() {
        throw std::runtime_error("---- tt_device::get_clocks is not implemented\n");
        return std::map<int,int>();
    }

    /**
     * @brief Get the PCIe speed for a specific device based on link width and link speed
     * \returns Bandwidth in Gbps
     */
    virtual std::uint32_t get_pcie_speed(std::uint32_t device_id) {
        return 8 * 16;  // default to x8 at 16 GT/s
    }

    /**
     * @brief Get the ethernet firmware version used by the physical cluster (only implemented for Silicon Backend)
     * \returns Firmware version {major, minor, patch}
    */
    virtual tt_version get_ethernet_fw_version() const {
        throw std::runtime_error("---- tt_device::get_ethernet_fw_version is not implemented \n");
    }

    /** 
     * @brief Get the total hugepage (host memory) size allocated for a device. 
     * This memory is not entirely accessible by device. To query the number of channels
     * or memory per channel that is accessbile, see get_host_channel_size or get_num_host_channels
     * \param src_device_id Device for which allocated host memory is being queried
     * \returns Total memory allocated on host for a specific device
     * 
    */ 
    virtual uint32_t dma_allocation_size(chip_id_t src_device_id = -1) {
        throw std::runtime_error("---- tt_device::dma_allocation_size is not implemented\n");
        return 0;
    }

    /** 
     * Get the address for the MMIO mapped region on Channel (as seen from host memory)
     * \param offset Address in DRAM
     * \param device_id logical id for MMIO device being queried
     * \returns Host interpretation of MMIO mapped channel 0 address 
     */ 
    virtual void *channel_0_address(std::uint32_t offset, std::uint32_t device_id) const {
        throw std::runtime_error("---- tt_device::channel_0_address is not implemented\n");
        return nullptr;
    }
    /**
     * @brief Query number of DRAM channels on a specific device
     * \param device_id Logical device id to query
     * \returns Number of DRAM channels on device
    */ 
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id) {
        throw std::runtime_error("---- tt_device::get_num_dram_channels is not implemented\n");
        return 0;
    }
    /**
     * @brief Get size for a specific DRAM channel on a device
     * \param device_id Logical device id to query
     * \param channel Logical channel id (taken from soc descriptor) for which the size will be queried
     * \returns Size of specific DRAM channel
    */ 
    virtual std::uint32_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
        throw std::runtime_error("---- tt_device::get_dram_channel_size is not implemented\n");
        return 0;
    }

    /**
     * @brief Query number of Host channels (hugepages) allocated for a specific device
     * \param device_id Logical device id to query
     * \returns Number of Host channels allocated for device
    */ 
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id) {
        throw std::runtime_error("---- tt_device::get_num_host_channels is not implemented\n");
        return 0;
    }

    /**
     * @brief Get size for a specific Host channel accessible by the corresponding device
     * \param device_id Logical device id to query
     * \param channel Logical Host channel id for which the accessible  size will be queried
     * \returns Device accessible size of specific Host channel
    */ 
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
        throw std::runtime_error("---- tt_device::get_host_channel_size is not implemented\n");
        return 0;
    }
    /**
     * @brief Get absolute address corresponding to a zero based offset into a specific host
     * memory channel for a specific device
     * \param offset Zero based relative offset wrt the start of the channel's address space
     * src_device_id Device for which the host memory address will be queried
     * channel Host memory channel id for which the absolute address will be computed
    */
    virtual void *host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
        throw std::runtime_error("---- tt_device::host_dma_address is not implemented\n");
        return nullptr;
    }

    virtual std::uint64_t get_pcie_base_addr_from_device() const {
        throw std::runtime_error("---- tt_device::get_pcie_base_addr_from_device is not implemented\n");
        return 0;
    }
    const tt_SocDescriptor *get_soc_descriptor(chip_id_t chip) const;

    bool performed_harvesting = false;
    std::unordered_map<chip_id_t, uint32_t> harvested_rows_per_target = {};
    bool translation_tables_en = false;
    bool tlbs_init = false;

    protected:
    std::unordered_map<chip_id_t, tt_SocDescriptor> soc_descriptor_per_chip = {};
};

class c_versim_core;
namespace nuapi {namespace device {template <typename, typename>class Simulator;}}
namespace versim {
  struct VersimSimulatorState;
  using VersimSimulator = nuapi::device::Simulator<c_versim_core *, VersimSimulatorState>;
}

/**
 * @brief Versim Backend Class, derived from the tt_device class
 * Implements APIs to communicate with a simulated (using Verilator) Tenstorrent Device.
*/ 
class tt_VersimDevice: public tt_device
{
    public:
    virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_);
    virtual void set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_);
    tt_VersimDevice(const std::string &sdesc_path, const std::string &ndesc_path);
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors();
    virtual void start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool init_device, bool skip_driver_allocs);
    virtual void start_device(const tt_device_params &device_params);
    virtual void close_device();
    virtual void deassert_risc_reset();
    virtual void deassert_risc_reset_at_core(tt_cxy_pair core);
    virtual void assert_risc_reset();
    virtual void assert_risc_reset_at_core(tt_cxy_pair core);
    virtual void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    virtual void broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& columns_to_exclude, const std::string& fallback_tlb);
    virtual void rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
    virtual void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use);
    virtual void rolled_write_to_device(uint32_t* mem_ptr, uint32_t size_in_bytes, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb);
    virtual void write_to_device(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    virtual void read_from_device(void *mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use); 
    virtual void wait_for_non_mmio_flush();
    void l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels);
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c);
    virtual bool using_harvested_soc_descriptors();
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
    virtual bool noc_translation_en();
    virtual std::set<chip_id_t> get_target_mmio_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual ~tt_VersimDevice();
    virtual tt_ClusterDescriptor* get_cluster_description();
    virtual int get_number_of_chips_in_cluster();
    virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster();
    static int detect_number_of_chips();
    virtual std::map<int,int> get_clocks();
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id);
    virtual std::uint32_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id);
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
    private:
    bool stop();
    tt_device_l1_address_params l1_address_params;
    tt_device_dram_address_params dram_address_params;
    versim::VersimSimulator* versim;
    std::shared_ptr<tt_ClusterDescriptor> ndesc;
    void* p_ca_soc_manager;
};

#include "device/architecture_implementation.h"

/**
 * @brief Silicon Driver Class, derived from the tt_device class
 * Implements APIs to communicate with a physical Tenstorrent Device.
*/ 
class tt_SiliconDevice: public tt_device
{
    public:
    // Constructor
    /**
     * @brief Silicon Driver constructor.
     * \param sdesc_path Location of the SOC descriptor containing the default description of a single chip in the cluster (this does not have to account for product level changes such as harvesting).
     * \param ndesc_path Location of the Network Descriptor specifying the network topology of the system.
     * \param target_devices Logical Device ids being targeted by workload.
     * \param num_host_mem_ch_per_mmio_device Requested number of host channels (hugepages) per MMIO mapped device. The driver may allocated less per device, depending on availability.
     * \param dynamic_tlb_config_ Map specifying dynamic tlb names and the indices they correspond to
     * \param skip_driver_allocs Specifies if the Silicon Driver object should be initialized + started without modifying device state (ex: bringing device out of reset or shared host state (ex: initializing hugepages)
     * \param clean_system_resource Specifies if potentially corrupted shared host state from previous runs needs to be cleaned up. Should only be set by the main thread/process running
     * on a device. Setting this across multiple processes per device will cause issues since objects required by the driver will be cleared.
    * \param perform_harvesting Allow the driver to modify the SOC descriptors per chip by considering the harvesting configuration of the cluster.
    */ 
    tt_SiliconDevice(const std::string &sdesc_path, const std::string &ndesc_path = "", const std::set<chip_id_t> &target_devices = {}, 
                    const uint32_t &num_host_mem_ch_per_mmio_device = 1, const std::unordered_map<std::string, std::int32_t>& dynamic_tlb_config_ = {}, 
                    const bool skip_driver_allocs = false, const bool clean_system_resources = false, bool perform_harvesting = true, std::unordered_map<chip_id_t, uint32_t> simulated_harvesting_masks = {});
    
    //Setup/Teardown Functions
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors();
    virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_);
    virtual void set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_);
    virtual void set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_);
    virtual void set_driver_eth_interface_params(const tt_driver_eth_interface_params& eth_interface_params_);
    virtual void configure_tlb(chip_id_t logical_device_id, tt_xy_pair core, std::int32_t tlb_index, std::int32_t address, uint64_t ordering = TLB_DATA::Posted);
    virtual void set_fallback_tlb_ordering_mode(const std::string& fallback_tlb, uint64_t ordering = TLB_DATA::Posted);
    virtual void setup_core_to_tlb_map(std::function<std::int32_t(tt_xy_pair)> mapping_function);
    virtual void start_device(const tt_device_params &device_params);
    virtual void assert_risc_reset();
    virtual void deassert_risc_reset();
    virtual void deassert_risc_reset_at_core(tt_cxy_pair core);
    virtual void assert_risc_reset_at_core(tt_cxy_pair core);
    virtual void close_device();

    // Runtime Functions
    virtual void write_to_device(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    virtual void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    void broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude,  std::set<uint32_t>& rows_to_exclude,  std::set<uint32_t>& columns_to_exclude, const std::string& fallback_tlb);
    virtual void write_epoch_cmd_to_device(const uint32_t *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write);
    virtual void write_epoch_cmd_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write);

    virtual void rolled_write_to_device(uint32_t* mem_ptr, uint32_t size_in_bytes, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb);
    virtual void rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
    virtual void read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    virtual void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use);
    virtual void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void write_to_sysmem(const void* mem_ptr, std::uint32_t size,  uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void read_from_sysmem(std::vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    virtual void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    virtual void wait_for_non_mmio_flush();
    void l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels);
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
    // These functions are used by Debuda, so make them public
    void bar_write32 (int logical_device_id, uint32_t addr, uint32_t data);
    uint32_t bar_read32 (int logical_device_id, uint32_t addr);
    /**
     * @brief If the tlbs are initialized, returns a tuple with the TLB base address and its size
    */
    std::optional<std::tuple<uint32_t, uint32_t>> get_tlb_data_from_target(const tt_xy_pair& target);
    /**
     * @brief This API allows you to write directly to device memory that is addressable by a static TLB
    */
    std::function<void(uint32_t, uint32_t, const uint8_t*, uint32_t)> get_fast_pcie_static_tlb_write_callable(int device_id);
    /**
     * @brief Returns the DMA buf size 
    */
    uint32_t get_m_dma_buf_size() const;
    // Misc. Functions to Query/Set Device State
    virtual int arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr);
    virtual bool using_harvested_soc_descriptors();
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
    virtual bool noc_translation_en();
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c);
    virtual int get_number_of_chips_in_cluster();
    virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster();
    virtual tt_ClusterDescriptor* get_cluster_description();
    static int detect_number_of_chips();
    static std::vector<chip_id_t> detect_available_device_ids();
    static std::unordered_map<chip_id_t, chip_id_t> get_logical_to_physical_mmio_device_id_map(std::vector<chip_id_t> physical_device_ids);
    virtual std::set<chip_id_t> get_target_mmio_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual std::map<int,int> get_clocks();
    virtual uint32_t dma_allocation_size(chip_id_t src_device_id = -1);
    virtual void *channel_0_address(std::uint32_t offset, std::uint32_t device_id) const;
    virtual void *host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;
    virtual std::uint64_t get_pcie_base_addr_from_device() const;
    static std::vector<int> extract_rows_to_remove(const tt::ARCH &arch, const int worker_grid_rows, const int harvested_rows);
    static void remove_worker_row_from_descriptor(tt_SocDescriptor& full_soc_descriptor, const std::vector<int>& row_coordinates_to_remove);
    static void harvest_rows_in_soc_descriptor(tt::ARCH arch, tt_SocDescriptor& sdesc, uint32_t harvested_rows);
    static std::unordered_map<tt_xy_pair, tt_xy_pair> create_harvested_coord_translation(const tt::ARCH arch, bool identity_map);
    static std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_from_harvested_rows(std::unordered_map<chip_id_t, std::vector<uint32_t>> harvested_rows); 
    std::unordered_map<tt_xy_pair, tt_xy_pair> get_harvested_coord_translation_map(chip_id_t logical_device_id);
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id);
    virtual std::uint32_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id);
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_pcie_speed(std::uint32_t device_id);
    virtual tt_version get_ethernet_fw_version() const;

    // Destructor
    virtual ~tt_SiliconDevice ();

    private:
    // Helper functions
    // Startup + teardown
    void create_device(const std::unordered_set<chip_id_t> &target_mmio_device_ids, const uint32_t &num_host_mem_ch_per_mmio_device, const bool skip_driver_allocs, const bool clean_system_resources);
    void initialize_interprocess_mutexes(int pci_interface_id, bool cleanup_mutexes_in_shm);
    void cleanup_shared_host_state();
    void initialize_pcie_devices();
    void broadcast_pcie_tensix_risc_reset(struct PCIdevice *device, const TensixSoftResetOptions &cores);
    void broadcast_tensix_risc_reset_to_cluster(const TensixSoftResetOptions &soft_resets);
    void send_remote_tensix_risc_reset_to_core(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets);
    void send_tensix_risc_reset_to_core(const tt_cxy_pair &core, const TensixSoftResetOptions &soft_resets);
    void perform_harvesting_and_populate_soc_descriptors(const std::string& sdesc_path, const bool perform_harvesting);
    void populate_cores();
    void init_pcie_iatus();
    void init_pcie_iatus_no_p2p();
    bool init_hugepage(chip_id_t device_id);
    bool init_dmabuf(chip_id_t device_id);
    void check_pcie_device_initialized(int device_id);
    bool init_dma_turbo_buf(struct PCIdevice* pci_device);
    bool uninit_dma_turbo_buf(struct PCIdevice* pci_device);
    static std::map<chip_id_t, std::string> get_physical_device_id_to_bus_id_map(std::vector<chip_id_t> physical_device_ids);
    void set_pcie_power_state(tt_DevicePowerState state);
    int set_remote_power_state(const chip_id_t &chip, tt_DevicePowerState device_state);
    void set_power_state(tt_DevicePowerState state);
    uint32_t get_power_state_arc_msg(struct PCIdevice* pci_device, tt_DevicePowerState state);
    void enable_local_ethernet_queue(const chip_id_t& chip, int timeout);
    void enable_ethernet_queue(int timeout);
    void enable_remote_ethernet_queue(const chip_id_t& chip, int timeout);
    void deassert_resets_and_set_power_state();
    int open_hugepage_file(const std::string &dir, chip_id_t device_id, uint16_t channel);
    int iatu_configure_peer_region (int logical_device_id, uint32_t peer_region_id, uint64_t bar_addr_64, uint32_t region_size);
    uint32_t get_harvested_noc_rows (uint32_t harvesting_mask);
    uint32_t get_harvested_rows (int logical_device_id);
    int get_clock(int logical_device_id);

    // Communication Functions
    void read_dma_buffer(void* mem_ptr, std::uint32_t address, std::uint16_t channel, std::uint32_t size_in_bytes, chip_id_t src_device_id);
    void write_dma_buffer(const void *mem_ptr, std::uint32_t size, std::uint32_t address, std::uint16_t channel, chip_id_t src_device_id);
    void write_device_memory(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair target, std::uint32_t address, const std::string& fallback_tlb);
    void write_to_non_mmio_device(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t address, bool broadcast = false, std::vector<int> broadcast_header = {});
    void read_device_memory(void *mem_ptr, tt_cxy_pair target, std::uint32_t address, std::uint32_t size_in_bytes, const std::string& fallback_tlb);
    void write_to_non_mmio_device_send_epoch_cmd(const uint32_t *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t address, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write);
    void rolled_write_to_non_mmio_device(const uint32_t *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t address, uint32_t unroll_count);
    void read_from_non_mmio_device(void* mem_ptr, tt_cxy_pair core, uint64_t address, uint32_t size_in_bytes);
    void read_mmio_device_register(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    void write_mmio_device_register(const void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    void pcie_broadcast_write(chip_id_t chip, const void* mem_ptr, uint32_t size_in_bytes, std::uint32_t addr, const tt_xy_pair& start, const tt_xy_pair& end, const std::string& fallback_tlb);
    void ethernet_broadcast_write(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude, const std::set<uint32_t>& rows_to_exclude, 
                                  std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb, bool use_virtual_coords);
    void set_membar_flag(const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores, const uint32_t barrier_value, const uint32_t barrier_addr, const std::string& fallback_tlb);
    void insert_host_to_device_barrier(const chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores, const uint32_t barrier_addr, const std::string& fallback_tlb);
    void init_membars();
    uint64_t get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint64_t offset);
    uint16_t get_sys_rack(uint32_t rack_x, uint32_t rack_y);
    bool is_non_mmio_cmd_q_full(uint32_t curr_wptr, uint32_t curr_rptr);
    int pcie_arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr);
    int remote_arc_msg(int logical_device_id, uint32_t msg_code, bool wait_for_done = true, uint32_t arg0 = 0, uint32_t arg1 = 0, int timeout=1, uint32_t *return_3 = nullptr, uint32_t *return_4 = nullptr);
    bool address_in_tlb_space(uint32_t address, uint32_t size_in_bytes, int32_t tlb_index, uint32_t tlb_size, uint32_t chip);
    struct PCIdevice* get_pci_device(int pci_intf_id) const;
    std::shared_ptr<boost::interprocess::named_mutex> get_mutex(const std::string& tlb_name, int pci_interface_id);
    virtual uint32_t get_harvested_noc_rows_for_chip(int logical_device_id); // Returns one-hot encoded harvesting mask for PCIe mapped chips
    void generate_tensix_broadcast_grids_for_grayskull( std::set<std::pair<tt_xy_pair, tt_xy_pair>>& broadcast_grids, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude);
    std::unordered_map<chip_id_t, std::vector<std::vector<int>>>&  get_ethernet_broadcast_headers(const std::set<chip_id_t>& chips_to_exclude);
    // Test functions
    void verify_eth_fw();
    void verify_sw_fw_versions(int device_id, std::uint32_t sw_version, std::vector<std::uint32_t> &fw_versions);
    int test_pcie_tlb_setup (struct PCIdevice* pci_device);
    int test_setup_interface ();
    int test_broadcast (int logical_device_id);

    // State variables
    tt_device_dram_address_params dram_address_params;
    tt_device_l1_address_params l1_address_params;
    tt_driver_host_address_params host_address_params;
    tt_driver_eth_interface_params eth_interface_params;
    std::vector<tt::ARCH> archs_in_cluster = {};
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    tt_SocDescriptor& get_soc_descriptor(chip_id_t chip_id);
    tt::ARCH arch_name;
    std::map<chip_id_t, struct PCIdevice*> m_pci_device_map;    // Map of enabled pci devices
    int m_num_pci_devices;                                      // Number of pci devices in system (enabled or disabled)
    std::shared_ptr<tt_ClusterDescriptor> ndesc;
    // Level of printouts. Controlled by env var TT_PCI_LOG_LEVEL
    // 0: no debugging messages, 1: less verbose, 2: more verbose
    int m_pci_log_level;

    // remote eth transfer setup
    static constexpr std::uint32_t NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 6;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS = 4;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_START_ID = 0;
    static constexpr std::uint32_t NON_EPOCH_ETH_CORES_MASK = (NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS-1);

    static constexpr std::uint32_t EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS = NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS - NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_START_ID = NON_EPOCH_ETH_CORES_START_ID + NON_EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS;
    static constexpr std::uint32_t EPOCH_ETH_CORES_MASK = (EPOCH_ETH_CORES_FOR_NON_MMIO_TRANSFERS-1);

    int active_core = NON_EPOCH_ETH_CORES_START_ID;
    int active_core_epoch = EPOCH_ETH_CORES_START_ID;
    bool erisc_q_ptrs_initialized = false;
    std::vector<std::uint32_t> erisc_q_ptrs_epoch[NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS];
    bool erisc_q_wrptr_updated[NUM_ETH_CORES_FOR_NON_MMIO_TRANSFERS];
    std::vector< std::vector<tt_cxy_pair> > remote_transfer_ethernet_cores;
    bool flush_non_mmio = false;
    // Size of the PCIE DMA buffer
    // The setting should not exceed MAX_DMA_BYTES
    std::uint32_t m_dma_buf_size;
    std::unordered_map<chip_id_t, bool> noc_translation_enabled_for_chip = {};
    std::map<std::string, std::shared_ptr<boost::interprocess::named_mutex>> hardware_resource_mutex_map = {};
    std::unordered_map<chip_id_t, std::unordered_map<tt_xy_pair, tt_xy_pair>> harvested_coord_translation = {};
    std::unordered_map<chip_id_t, std::uint32_t> num_rows_harvested = {};
    std::unordered_map<chip_id_t, std::unordered_set<tt_xy_pair>> workers_per_chip = {};
    std::unordered_set<tt_xy_pair> eth_cores = {};
    std::unordered_set<tt_xy_pair> dram_cores = {};
    uint32_t m_num_host_mem_channels = 0;
    std::unordered_map<chip_id_t, std::unordered_map<int, void *>> hugepage_mapping;
    std::unordered_map<chip_id_t, std::unordered_map<int, std::size_t>> hugepage_mapping_size;
    std::unordered_map<chip_id_t, std::unordered_map<int, std::uint64_t>> hugepage_physical_address;
    std::map<chip_id_t, std::unordered_map<std::int32_t, std::int32_t>> tlb_config_map = {};
    std::set<chip_id_t> all_target_mmio_devices;
    std::unordered_map<chip_id_t, std::vector<uint32_t>> host_channel_size;
    std::function<std::int32_t(tt_xy_pair)> map_core_to_tlb;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {};
    std::unordered_map<std::string, uint64_t> dynamic_tlb_ordering_modes = {};
    std::map<std::set<chip_id_t>, std::unordered_map<chip_id_t, std::vector<std::vector<int>>>> bcast_header_cache = {};
    std::uint64_t buf_physical_addr = 0;
    void * buf_mapping = nullptr;
    int driver_id;  
    bool perform_harvesting_on_sdesc = false;
    bool use_ethernet_ordered_writes = true;
    bool use_ethernet_broadcast = true;
    bool use_virtual_coords_for_eth_broadcast = true;
    tt_version eth_fw_version; // Ethernet FW the driver is interfacing with
    // Named Mutexes
    static constexpr char NON_MMIO_MUTEX_NAME[] = "NON_MMIO";
    static constexpr char ARC_MSG_MUTEX_NAME[] = "ARC_MSG";
    static constexpr char MEM_BARRIER_MUTEX_NAME[] = "MEM_BAR";
    // ERISC FW Version Required by UMD
    static constexpr std::uint32_t SW_VERSION = 0x06060000;
};

tt::ARCH detect_arch(uint16_t device_id = 0);

uint32_t get_num_hugepages();

constexpr inline bool operator==(const tt_version &a, const tt_version &b) {
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

constexpr inline bool operator>=(const tt_version &a, const tt_version &b) {
    bool fw_major_greater = a.major > b.major;
    bool fw_minor_greater = (a.major == b.major) && (a.minor > b.minor);
    bool patch_greater_or_equal = (a.major == b.major) && (a.minor == b.minor) && (a.patch >= b.patch);
    return fw_major_greater || fw_minor_greater || patch_greater_or_equal;
}
