/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "common_types.h"
#include "tlb.h"
#include "soc_descriptor.h"

// brosko, why would chip know about cluster descriptor?
#include "cluster_descriptor.h"

#include "tt_io.hpp"

#include <cassert>

#include <string>
#include <stdexcept>
#include <functional>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <map>

namespace tt::umd {

enum class TensixSoftResetOptions: std::uint32_t {
    NONE = 0,
    BRISC = ((std::uint32_t) 1 << 11),
    TRISC0 = ((std::uint32_t) 1 << 12),
    TRISC1 = ((std::uint32_t) 1 << 13),
    TRISC2 = ((std::uint32_t) 1 << 14),
    NCRISC = ((std::uint32_t) 1 << 18),
    STAGGERED_START = ((std::uint32_t) 1 << 31)
};

std::string TensixSoftResetOptionsToString(TensixSoftResetOptions value);
constexpr TensixSoftResetOptions operator|(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return static_cast<TensixSoftResetOptions>(
        static_cast<uint32_t>(lhs) |
        static_cast<uint32_t>(rhs)
    );
}

constexpr TensixSoftResetOptions operator&(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return static_cast<TensixSoftResetOptions>(
        static_cast<uint32_t>(lhs) &
        static_cast<uint32_t>(rhs)
    );
}

constexpr bool operator!=(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return
        static_cast<uint32_t>(lhs) !=
        static_cast<uint32_t>(rhs);
}

static constexpr TensixSoftResetOptions ALL_TRISC_SOFT_RESET = TensixSoftResetOptions::TRISC0 |
                                                           TensixSoftResetOptions::TRISC1 |
                                                           TensixSoftResetOptions::TRISC2;

static constexpr TensixSoftResetOptions ALL_TENSIX_SOFT_RESET = TensixSoftResetOptions::BRISC |
                                                            TensixSoftResetOptions::NCRISC |
                                                            TensixSoftResetOptions::STAGGERED_START |
                                                            ALL_TRISC_SOFT_RESET;

static constexpr TensixSoftResetOptions TENSIX_ASSERT_SOFT_RESET = TensixSoftResetOptions::BRISC |
                                                               TensixSoftResetOptions::NCRISC |
                                                               ALL_TRISC_SOFT_RESET;

static constexpr TensixSoftResetOptions TENSIX_DEASSERT_SOFT_RESET = TensixSoftResetOptions::NCRISC |
                                                                 ALL_TRISC_SOFT_RESET |
                                                                 TensixSoftResetOptions::STAGGERED_START;

static constexpr TensixSoftResetOptions TENSIX_DEASSERT_SOFT_RESET_NO_STAGGER = TensixSoftResetOptions::NCRISC |
                                                                                 ALL_TRISC_SOFT_RESET;


enum DevicePowerState {
    BUSY,
    SHORT_IDLE,
    LONG_IDLE
};

struct device_dram_address_params {
    std::uint32_t DRAM_BARRIER_BASE = 0;
};

/**
 * @brief Struct encapsulating all L1 Address Map parameters required by UMD.
 * These parameters are passed to the constructor.
*/
struct device_l1_address_params {
    std::uint32_t ncrisc_fw_base = 0;
    std::uint32_t fw_base = 0;
    std::uint32_t trisc0_size = 0;
    std::uint32_t trisc1_size = 0;
    std::uint32_t trisc2_size = 0;
    std::uint32_t trisc_base = 0;
    std::uint32_t tensix_l1_barrier_base = 0;
    std::uint32_t eth_l1_barrier_base = 0;
    std::uint32_t fw_version_addr = 0;
};


/**
 * @brief Struct encapsulating all Host Address Map parameters required by UMD.
 * These parameters are passed to the constructor and are needed for non-MMIO transactions.
*/
struct driver_host_address_params {
    std::uint32_t eth_routing_block_size = 0;
    std::uint32_t eth_routing_buffers_start = 0;
};

/**
 * @brief Struct encapsulating all ERISC Firmware parameters required by UMD.
 * These parameters are passed to the constructor and are needed for non-MMIO transactions.
*/
struct driver_eth_interface_params {
    std::uint32_t noc_addr_local_bits = 0;
    std::uint32_t noc_addr_node_id_bits = 0;
    std::uint32_t eth_rack_coord_width = 0;
    std::uint32_t cmd_buf_size_mask = 0;
    std::uint32_t max_block_size = 0;
    std::uint32_t request_cmd_queue_base = 0;
    std::uint32_t response_cmd_queue_base = 0;
    std::uint32_t cmd_counters_size_bytes = 0;
    std::uint32_t remote_update_ptr_size_bytes = 0;
    std::uint32_t cmd_data_block = 0;
    std::uint32_t cmd_wr_req = 0;
    std::uint32_t cmd_wr_ack = 0;
    std::uint32_t cmd_rd_req = 0;
    std::uint32_t cmd_rd_data = 0;
    std::uint32_t cmd_buf_size = 0;
    std::uint32_t cmd_data_block_dram = 0;
    std::uint32_t eth_routing_data_buffer_addr = 0;
    std::uint32_t request_routing_cmd_queue_base = 0;
    std::uint32_t response_routing_cmd_queue_base = 0;
    std::uint32_t cmd_buf_ptr_mask = 0;
    std::uint32_t cmd_ordered = 0;
    std::uint32_t cmd_broadcast = 0;
};


struct device_params {
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
    std::vector<std::string> unroll_vcd_dump_cores(xy_pair grid_size) const {
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


constexpr inline bool operator==(const tt_version &a, const tt_version &b) {
    return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

constexpr inline bool operator>=(const tt_version &a, const tt_version &b) {
    bool fw_major_greater = a.major > b.major;
    bool fw_minor_greater = (a.major == b.major) && (a.minor > b.minor);
    bool patch_greater_or_equal = (a.major == b.major) && (a.minor == b.minor) && (a.patch >= b.patch);
    return fw_major_greater || fw_minor_greater || patch_greater_or_equal;
}


// brosko: remove chip_id from whole interface

    /**
     * @brief Parent class for tt_SiliconDevice (Silicon Driver) and tt_VersimDevice (Versim Backend API).
     * Exposes a generic interface to callers, providing declarations for virtual functions defined differently for
     * Silicon and Versim.
     * Valid usage consists of declaring a tt_device object and initializing it to either a Silicon or Versim backend.
     * Using tt_device itself will throw errors, since its APIs are undefined.
     */
    class Chip {
       public:
        Chip();
        virtual ~Chip();
        // Setup/Teardown Functions
        /**
         * @brief Set L1 Address Map parameters used by UMD to communicate with the TT Device
         * \param l1_address_params_ device_l1_address_params encapsulating all the L1 parameters required by UMD
         */
        virtual void set_device_l1_address_params(const device_l1_address_params& l1_address_params_) {
            throw std::runtime_error("---- tt_device::set_device_l1_address_params is not implemented\n");
        }

        virtual void set_device_dram_address_params(const device_dram_address_params& dram_address_params_) {
            throw std::runtime_error("---- tt_device::set_device_dram_address_params is not implemented\n");
        }

        /**
         * @brief Set Host Address Map parameters used by UMD to communicate with the TT Device (used for remote
         * transactions) \param host_address_params_ driver_host_address_params encapsulating all the Host Address
         * space parameters required by UMD
         */

        virtual void set_driver_host_address_params(const driver_host_address_params& host_address_params_) {
            throw std::runtime_error("---- tt_device::set_driver_host_address_params is not implemented\n");
        }

        /**
         * @brief Set ERISC Firmware parameters used by UMD to communicate with the TT Device (used for remote
         * transactions) \param eth_interface_params_ driver_eth_interface_params encapsulating all the Ethernet
         * Firmware parameters required by UMD
         */
        virtual void set_driver_eth_interface_params(const driver_eth_interface_params& eth_interface_params_) {
            throw std::runtime_error("---- tt_device::set_driver_eth_interface_params is not implemented\n");
        }

        /**
         * @brief Configure a TLB to point to a specific core and an address within that core. Should be done for Static
         * TLBs \param logical_device_id Logical Device being targeted \param core The TLB will be programmed to point
         * to this core \param tlb_index TLB id that will be programmed \param address All incoming transactions to the
         * TLB will be routed to an address space starting at this parameter (after its aligned to the TLB size) \param
         * ordering Ordering mode for the TLB. Can be Strict (ordered and blocking, since this waits for ack -> slow),
         * Relaxed (ordered, but non blocking -> fast) or Posted (no ordering, non blocking -> fastest).
         */
        virtual void configure_tlb(
            chip_id_t logical_device_id,
            xy_pair core,
            std::int32_t tlb_index,
            std::int32_t address,
            uint64_t ordering = tlb_data::Relaxed) {
            throw std::runtime_error("---- tt_device::configure_tlb is not implemented\n");
        }

        /**
         * @brief Set ordering mode for dynamic/fallback TLBs (passed into driver constructor)
         * \param fallback_tlb Dynamic TLB being targeted
         * \param ordering Ordering mode for the TLB. Can be Strict (ordered and blocking, since this waits for ack ->
         * slow), Posted (ordered, but non blocking -> fast) or Relaxed (no ordering, non blocking -> fastest).
         */
        virtual void set_fallback_tlb_ordering_mode(
            const std::string& fallback_tlb, uint64_t ordering = tlb_data::Posted) {
            throw std::runtime_error("---- tt_device::set_fallback_tlb_ordering_mode is not implemented\n");
        }
        /**
         * @brief Give UMD a 1:1 function mapping a core to its appropriate static TLB (currently only support a single
         * TLB per core). \param mapping_function An std::function object with xy_pair as an input, returning the
         * int32_t TLB index for the input core. If the core does not have a mapped TLB, the function should return -1.
         */
        virtual void setup_core_to_tlb_map(std::function<std::int32_t(xy_pair)> mapping_function) {
            throw std::runtime_error("---- tt_device::setup_core_to_tlb_map is not implemented\n");
        }
        /**
         * @brief Pass in ethernet cores with active links for a specific MMIO chip. When called, this function will
         * force UMD to use a subset of cores from the active_eth_cores_per_chip set for all host->cluster non-MMIO
         * transfers. If this function is not called, UMD will use a default set of ethernet core indices for these
         * transfers (0 through 5). If default behaviour is not desired, this function must be called for all MMIO
         * devices. \param mmio_chip MMIO device for which the active ethernet cores are being set \param
         * active_eth_cores_per_chip The active ethernet cores for this chip
         */
        virtual void configure_active_ethernet_cores_for_mmio_device(
            chip_id_t mmio_chip, const std::unordered_set<xy_pair>& active_eth_cores_per_chip) {
            throw std::runtime_error(
                "---- tt_device::configure_active_ethernet_cores_for_mmio_device is not implemented\n");
        }
        /**
         * @brief Start the Silicon on Versim Device
         * On Silicon: Assert soft Tensix reset, deassert RiscV reset, set power state to busy (ramp up AICLK),
         * initialize iATUs for PCIe devices and ethernet queues for remote chips. \param device_params device_params
         * object specifying initialization configuration
         */
        virtual void start_device(const device_params& device_params) {
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
         * \param core cxy_pair specifying the chip and core being targeted
         */
        virtual void deassert_risc_reset_at_core(cxy_pair core) {
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
         * \param core cxy_pair specifying the chip and core being targeted
         */
        virtual void assert_risc_reset_at_core(cxy_pair core) {
            throw std::runtime_error("---- tt_device::assert_risc_reset_at_core is not implemented\n");
        }
        /**
         * @brief To be called at the end of a run.
         * Set power state to idle, assert tensix reset at all cores.
         */
        virtual void close_device() { throw std::runtime_error("---- tt_device::close_device is not implemented\n"); }

        // Runtime functions
        /**
         * @brief Non-MMIO (ethernet) barrier.
         * Similar to an mfence for host -> host transfers. Will flush all in-flight ethernet transactions before
         * proceeding with the next one.
         */
        virtual void wait_for_non_mmio_flush() {
            throw std::runtime_error("---- tt_device::wait_for_non_mmio_flush is not implemented\n");
        }
        /**
         * @brief Write uint32_t data (as specified by ptr + len pair) to specified device, core and address (defined
         * for Silicon). \param mem_ptr src data address \param len src data size (specified for uint32_t) \param core
         * chip-x-y struct specifying device and core \param addr Address to write to \param tlb_to_use Specifies
         * fallback/dynamic TLB to use for transaction, if this core does not have static TLBs mapped to this address
         * (dynamic TLBs were initialized in driver constructor)
         */
        virtual void write_to_device(
            const void* mem_ptr,
            uint32_t size_in_bytes,
            cxy_pair core,
            uint64_t addr,
            const std::string& tlb_to_use) {
            // Only implement this for Silicon Backend
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
         * @brief Write uint32_t vector to specified device, core and address (defined for Silicon and Versim).
         * \param vec Vector to write
         * \param core chip-x-y struct specifying device and core
         * \param addr Address to write to
         * \param tlb_to_use Specifies fallback/dynamic TLB to use for transaction, if this core does not have static
         * TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
         */
        virtual void write_to_device(
            std::vector<uint32_t>& vec, cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
            throw std::runtime_error("---- tt_device::write_to_device is not implemented\n");
        }

        /**
         * @brief Read uint32_t data from a specified device, core and address to host memory (defined for Silicon).
         * \param mem_ptr dest data address on host (expected to be preallocated, depending on transfer size)
         * \param core chip-x-y struct specifying device and core
         * \param addr Address to read from
         * \param size Read Size
         * \param fallback_tlb Specifies fallback/dynamic TLB to use for transaction, if this core does not have static
         * TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
         */
        virtual void read_from_device(
            void* mem_ptr, cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
            // Only implement this for Silicon Backend
            throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
        }

        /**
         * @brief Read a uint32_t vector from a specified device, core and address to host memory (defined for Silicon
         * and Versim). \param vec host side vector to populate with data read from device (does not need to be
         * preallocated) \param core chip-x-y struct specifying device and core \param addr Address to read from \param
         * size Read Size \param fallback_tlb Specifies fallback/dynamic TLB to use for transaction, if this core does
         * not have static TLBs mapped to this address (dynamic TLBs were initialized in driver constructor)
         */
        virtual void read_from_device(
            std::vector<uint32_t>& vec, cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {
            throw std::runtime_error("---- tt_device::read_from_device is not implemented\n");
        }

        /**
         * @brief Write uint32_t vector to specified address and channel on host (defined for Silicon).
         * \param vec Vector to write
         * \param addr Address to write to
         * \param channel Host channel to target (each MMIO Mapped chip has its own set of channels)
         * \param src_device_id Chip level specifier identifying which chip's host address space needs to be targeted
         */
        virtual void write_to_sysmem(
            std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
            throw std::runtime_error("---- tt_device::write_to_sysmem is not implemented\n");
        }

        virtual void write_to_sysmem(
            const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) {
            throw std::runtime_error("---- tt_device::write_to_sysmem is not implemented\n");
        }
        /**
         * @brief Read uint32_t vector from specified address and channel on host (defined for Silicon).
         * \param vec Vector to read (does not need to be preallocated)
         * \param addr Address to read from
         * \param channel Host channel to query (each MMIO Mapped chip has its own set of channels)
         * \param src_device_id Chip level specifier identifying which chip's host address space needs to be queried
         */
        virtual void read_from_sysmem(
            std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
            throw std::runtime_error("---- tt_device::read_from_sysmem is not implemented\n");
        }
        virtual void read_from_sysmem(
            void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) {
            throw std::runtime_error("---- tt_device::read_from_sysmem is not implemented\n");
        }
        virtual void l1_membar(
            const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<xy_pair>& cores = {}) {
            throw std::runtime_error("---- tt_device::l1_membar is not implemented\n");
        }
        virtual void dram_membar(
            const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels = {}) {
            throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
        }
        virtual void dram_membar(
            const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<xy_pair>& cores = {}) {
            throw std::runtime_error("---- tt_device::dram_membar is not implemented\n");
        }

        // Misc. Functions to Query/Set Device State
        /**
         * @brief Query post harvesting SOC descriptors from UMD in virtual coordinates.
         * These descriptors should be used for looking up cores that are passed into UMD APIs.
         * \returns A map of SOC Descriptors per chip.
         */
        virtual std::unordered_map<chip_id_t, SocDescriptor>& get_virtual_soc_descriptors() {
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
         * @brief Translate between virtual coordinates (from UMD SOC Descriptor) and Translated Coordinates
         * \param device_id Logical Device for which the core coordinates need to be transalted
         * \param r Row coordinate
         * \param c Column coordinate
         */
        virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c) {
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
        virtual ClusterDescriptor* get_cluster_description() {
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
         * @brief Get clock frequencies for all MMIO devices ftargeted by UMD
         * \returns Map of logical chip id to clock frequency (for MMIO chips only)
         */
        virtual std::map<int, int> get_clocks() {
            throw std::runtime_error("---- tt_device::get_clocks is not implemented\n");
            return std::map<int, int>();
        }

        virtual std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id) {
            throw std::runtime_error("---- tt_device::get_numa_node_for_pcie_device is not implemented\n");
        }

        /**
         * @brief Get the ethernet firmware version used by the physical cluster (only implemented for Silicon Backend)
         * \returns Firmware version {major, minor, patch}
         */
        virtual tt_version get_ethernet_fw_version() const {
            throw std::runtime_error("---- tt_device::get_ethernet_fw_version is not implemented \n");
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
        virtual std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
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
        virtual void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
            throw std::runtime_error("---- tt_device::host_dma_address is not implemented\n");
            return nullptr;
        }

        virtual std::uint64_t get_pcie_base_addr_from_device() const {
            throw std::runtime_error("---- tt_device::get_pcie_base_addr_from_device is not implemented\n");
            return 0;
        }
        const SocDescriptor* get_soc_descriptor(chip_id_t chip) const;

        bool performed_harvesting = false;
        std::unordered_map<chip_id_t, uint32_t> harvested_rows_per_target = {};
        bool translation_tables_en = false;
        bool tlbs_init = false;

       protected:
        std::unordered_map<chip_id_t, SocDescriptor> soc_descriptor_per_chip = {};
    };

}  // namespace tt::umd