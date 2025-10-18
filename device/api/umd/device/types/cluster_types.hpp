/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>

#include <cassert>
#include <ostream>
#include <vector>

namespace tt::umd {

struct DeviceParams {
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
        for (auto& dump_core : vcd_dump_cores) {
            // If the input is a single *, then dump all cores.
            if (dump_core == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                    for (size_t y = 0; y < grid_size.y; y++) {
                        std::string current_core_coord = fmt::format("{}-{}", x, y);
                        if (std::find(
                                std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) ==
                            std::end(unrolled_dump_core)) {
                            unrolled_dump_core.push_back(current_core_coord);
                        }
                    }
                }
                continue;
            }
            // Each core coordinate must contain three characters: "core.x-core.y".
            assert(dump_core.size() <= 5);
            size_t delimiter_pos = dump_core.find('-');
            assert(delimiter_pos != std::string::npos);  // y-dim should exist in core coord.

            std::string core_dim_x = dump_core.substr(0, delimiter_pos);
            size_t core_dim_y_start = delimiter_pos + 1;
            std::string core_dim_y = dump_core.substr(core_dim_y_start, dump_core.length() - core_dim_y_start);

            if (core_dim_x == "*" && core_dim_y == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                    for (size_t y = 0; y < grid_size.y; y++) {
                        std::string current_core_coord = fmt::format("{}-{}", x, y);
                        if (std::find(
                                std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) ==
                            std::end(unrolled_dump_core)) {
                            unrolled_dump_core.push_back(current_core_coord);
                        }
                    }
                }
            } else if (core_dim_x == "*") {
                for (size_t x = 0; x < grid_size.x; x++) {
                    std::string current_core_coord = fmt::format("{}-{}", x, core_dim_y);
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) ==
                        std::end(unrolled_dump_core)) {
                        unrolled_dump_core.push_back(current_core_coord);
                    }
                }
            } else if (core_dim_y == "*") {
                for (size_t y = 0; y < grid_size.y; y++) {
                    std::string current_core_coord = fmt::format("{}-{}", core_dim_x, y);
                    if (std::find(std::begin(unrolled_dump_core), std::end(unrolled_dump_core), current_core_coord) ==
                        std::end(unrolled_dump_core)) {
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
        std::vector<std::string> all_plusargs{
            fmt::format("+enable_perf_scoreboard={}", enable_perf_scoreboard),
            fmt::format("+register_monitor={}", register_monitor)};

        all_plusargs.insert(all_plusargs.end(), plusargs.begin(), plusargs.end());

        return all_plusargs;
    }
};

enum DevicePowerState { BUSY, SHORT_IDLE, LONG_IDLE };

enum MemBarFlag {
    SET = 0xaa,
    RESET = 0xbb,
};

inline std::ostream& operator<<(std::ostream& os, const DevicePowerState power_state) {
    switch (power_state) {
        case DevicePowerState::BUSY:
            os << "Busy";
            break;
        case DevicePowerState::SHORT_IDLE:
            os << "SHORT_IDLE";
            break;
        case DevicePowerState::LONG_IDLE:
            os << "LONG_IDLE";
            break;
        default:
            throw("Unknown DevicePowerState");
    }
    return os;
}

struct BarrierAddressParams {
    std::uint32_t tensix_l1_barrier_base = 0;
    std::uint32_t eth_l1_barrier_base = 0;
    std::uint32_t dram_barrier_base = 0;
};

struct DeviceDramAddressParams {
    std::uint32_t DRAM_BARRIER_BASE = 0;
};

/**
 * Struct encapsulating all L1 Address Map parameters required by UMD.
 * These parameters are passed to the constructor.
 */
struct DeviceL1AddressParams {
    std::uint32_t tensix_l1_barrier_base = 0;
    std::uint32_t eth_l1_barrier_base = 0;
    std::uint32_t fw_version_addr = 0;
};

/**
 * Struct encapsulating all Host Address Map parameters required by UMD.
 * These parameters are passed to the constructor and are needed for non-MMIO transactions.
 */
struct DriverHostAddressParams {
    std::uint32_t eth_routing_block_size = 0;
    std::uint32_t eth_routing_buffers_start = 0;
};

struct DriverNocParams {
    std::uint32_t noc_addr_local_bits = 0;
    std::uint32_t noc_addr_node_id_bits = 0;
};

/**
 * Struct encapsulating all ERISC Firmware parameters required by UMD.
 * These parameters are passed to the constructor and are needed for non-MMIO transactions.
 */
struct DriverEthInterfaceParams {
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

struct HugepageMapping {
    void* mapping = nullptr;
    size_t mapping_size = 0;
    uint64_t physical_address = 0;  // or IOVA, if IOMMU is enabled
};

}  // namespace tt::umd

// TODO: To be removed once clients switch to namespace usage.
using barrier_address_params = tt::umd::BarrierAddressParams;
using device_params = tt::umd::DeviceParams;
using tt_device_params = tt::umd::DeviceParams;
using hugepage_mapping = tt::umd::HugepageMapping;
using device_dram_address_params = tt::umd::DeviceDramAddressParams;
using device_l1_address_params = tt::umd::DeviceL1AddressParams;
using driver_host_address_params = tt::umd::DriverHostAddressParams;
using driver_noc_params = tt::umd::DriverNocParams;
using driver_eth_interface_params = tt::umd::DriverEthInterfaceParams;
