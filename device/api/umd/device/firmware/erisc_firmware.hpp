/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "umd/device/utils/semver.hpp"

namespace tt::umd::erisc_firmware {

// ERISC FW versions required by UMD.
constexpr SemVer BH_MIN_ERISC_FW_SUPPORTED_VERSION = SemVer(1, 4, 1);
constexpr SemVer WH_MIN_ERISC_FW_SUPPORTED_VERSION = SemVer(6, 14, 0);

constexpr uint32_t BASE_FW_HEARTBEAT_SIGNATURE = 0xABCD;
constexpr uint32_t METAL_FW_HEARTBEAT_SIGNATURE = 0xAABB;
constexpr uint32_t FABRIC_HEARTBEAT_SIGNATURE = 0xDCBA;

// Layout of the ETH routing firmware command queues, used by RemoteCommunicationLegacyFirmware to issue remote
// (non-MMIO) reads and writes. These values are the same for all architectures which run the legacy routing firmware.
namespace eth_routing {

// ERISC L1 region holding the routing command queues, and the one holding the data blocks.
constexpr uint32_t ETH_ROUTING_STRUCT_ADDR = 0x11000;
constexpr uint32_t ETH_ROUTING_DATA_BUFFER_ADDR = 0x12000;

// Number of routing commands a queue can hold, must be a power of two.
constexpr uint32_t CMD_BUF_SIZE = 4;
constexpr uint32_t CMD_BUF_SIZE_MASK = CMD_BUF_SIZE - 1;
constexpr uint32_t CMD_BUF_PTR_MASK = (CMD_BUF_SIZE << 1) - 1;

constexpr uint32_t CMD_SIZE_BYTES = 32;                // sizeof(routing_cmd_t)
constexpr uint32_t REMOTE_UPDATE_PTR_SIZE_BYTES = 16;  // sizeof(remote_update_ptr_t)
constexpr uint32_t CMD_COUNTERS_SIZE_BYTES = 32;       // sizeof(cmd_counters_t)
// cmd_counters_t + wrptr + rdptr + routing_cmd_t[CMD_BUF_SIZE]
constexpr uint32_t CMD_Q_SIZE_BYTES =
    CMD_COUNTERS_SIZE_BYTES + 2 * REMOTE_UPDATE_PTR_SIZE_BYTES + CMD_BUF_SIZE * CMD_SIZE_BYTES;

// There are 16 64-bit latency counters at the beginning of the command queue region.
constexpr uint32_t REQUEST_CMD_QUEUE_BASE = ETH_ROUTING_STRUCT_ADDR + 128;
constexpr uint32_t REQUEST_ROUTING_CMD_QUEUE_BASE =
    REQUEST_CMD_QUEUE_BASE + CMD_COUNTERS_SIZE_BYTES + 2 * REMOTE_UPDATE_PTR_SIZE_BYTES;
constexpr uint32_t RESPONSE_CMD_QUEUE_BASE = REQUEST_CMD_QUEUE_BASE + 2 * CMD_Q_SIZE_BYTES;
constexpr uint32_t RESPONSE_ROUTING_CMD_QUEUE_BASE =
    RESPONSE_CMD_QUEUE_BASE + CMD_COUNTERS_SIZE_BYTES + 2 * REMOTE_UPDATE_PTR_SIZE_BYTES;

// Flags of routing_cmd_t.
constexpr uint32_t CMD_WR_REQ = 0x1 << 0;
constexpr uint32_t CMD_WR_ACK = 0x1 << 1;
constexpr uint32_t CMD_RD_REQ = 0x1 << 2;
constexpr uint32_t CMD_RD_DATA = 0x1 << 3;
constexpr uint32_t CMD_DATA_BLOCK_DRAM = 0x1 << 4;
constexpr uint32_t CMD_DATA_BLOCK = 0x1 << 6;
constexpr uint32_t CMD_BROADCAST = 0x1 << 7;
constexpr uint32_t CMD_ORDERED = 0x1 << 12;

// Largest data block a single routing command can carry, 1024 bytes.
constexpr uint32_t MAX_BLOCK_SIZE = 0x1 << 10;

// Width in bits of a rack coordinate inside a system address.
constexpr uint32_t ETH_RACK_COORD_WIDTH = 8;

// Host memory region the routing firmware uses for data blocks which don't fit in ERISC L1.
// 16 ethernet cores x 4 buffers per core, placed at the start of the host scratch region of host channel 0.
constexpr uint32_t ETH_ROUTING_BLOCK_SIZE = 32 * 1024;
constexpr uint32_t ETH_ROUTING_BUFFERS_START = 0x38000000;

}  // namespace eth_routing

}  // namespace tt::umd::erisc_firmware
