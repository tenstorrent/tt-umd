/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <set>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

class ChipConnection {
public:
    virtual ~ChipConnection() = default;

    virtual void write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) = 0;
    virtual void read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) = 0;

    virtual void write_to_device_reg(tt_xy_pair core, const void* src, uint64_t reg_dest, uint32_t size) = 0;
    virtual void read_from_device_reg(tt_xy_pair core, void* dest, uint64_t reg_src, uint32_t size) = 0;

    virtual void pre_initialization_hook() = 0;
    virtual void initialization_hook() = 0;
    virtual void post_initialization_hook() = 0;

    virtual void verify_initialization() = 0;

    virtual void start_connection() = 0;
    virtual void stop_connection() = 0;

    virtual void ethernet_broadcast_write(
        const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header) = 0;
    virtual void set_remote_transfer_ethernet_cores(const std::unordered_set<tt_xy_pair>& cores) = 0;
    virtual void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) = 0;
};

}  // namespace tt::umd
