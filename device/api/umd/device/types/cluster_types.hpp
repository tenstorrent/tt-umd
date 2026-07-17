// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <cassert>
#include <optional>
#include <ostream>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

struct DeviceParams {
    bool register_monitor = false;
    bool enable_perf_scoreboard = false;
    std::vector<std::string> vcd_dump_cores;
    std::vector<std::string> plusargs;
    bool init_device = true;
    bool early_open_device = false;
    int aiclk = 0;
    uint32_t dram_membar_subchannel = 0;
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
