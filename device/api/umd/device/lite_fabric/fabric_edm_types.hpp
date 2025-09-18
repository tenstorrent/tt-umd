// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

namespace lite_fabric {

struct WorkerXY {
    uint16_t x;
    uint16_t y;

    constexpr WorkerXY() : x(0), y(0) {}

    constexpr WorkerXY(uint16_t x, uint16_t y) : x(x), y(y) {}

    constexpr uint32_t to_uint32() const { return (y << 16) | x; }

    static constexpr WorkerXY from_uint32(uint32_t v) { return WorkerXY(v & 0xFFFF, (v >> 16) & 0xFFFF); }

    constexpr bool operator==(const WorkerXY& rhs) const { return x == rhs.x && y == rhs.y; }

    constexpr bool operator!=(const WorkerXY& rhs) const { return !(*this == rhs); }
};

struct EDMChannelWorkerLocationInfo {
    uint32_t worker_semaphore_address;
    uint32_t align_pad_0;  // Padding added for safe reading over noc
    uint32_t align_pad_1;
    uint32_t align_pad_2;

    uint32_t worker_teardown_semaphore_address;
    uint32_t align_pad_3;  // Padding added for safe reading over noc
    uint32_t align_pad_4;
    uint32_t align_pad_5;

    WorkerXY worker_xy;
    uint32_t align_pad_6;  // Padding added for safe reading over noc
    uint32_t align_pad_7;
    uint32_t align_pad_8;

    uint32_t edm_read_counter = 0;
    uint32_t align_pad_9;  // Padding added for safe reading over noc
    uint32_t align_pad_10;
    uint32_t align_pad_11;
};

static_assert(sizeof(EDMChannelWorkerLocationInfo) <= 64);

}  // namespace lite_fabric
}  // namespace tt::umd
