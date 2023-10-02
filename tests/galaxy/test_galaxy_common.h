/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#pragma once

#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "device/tt_device.h"
#include "device/tt_xy_pair.h"

using chip_id_t = int;
using ethernet_channel_t = int;
using eth_coord_t = std::tuple<int, int, int, int>;  // x, y, rack, shelf
struct tt_multichip_core_addr {
    tt_multichip_core_addr() : core{}, chip{}, addr{} {}
    tt_multichip_core_addr(chip_id_t chip, tt_xy_pair core, std::uint64_t addr) : core(core), chip(chip), addr(addr) {}

    tt_xy_pair core;
    chip_id_t chip;
    std::uint64_t addr;
    std::string str() const {
        std::stringstream ss;
        ss << std::hex << addr << std::dec;
        return "(chip=" + std::to_string(chip) + ",x=" + std::to_string(core.x) + ",y=" + std::to_string(core.y) +
               ",addr=0x" + ss.str() + ")";
    }
};

// SIMPLE DATAMOVEMENT API BASED ON UMD
// send one contiguous chunk of data from one sender core to a receiver core
void move_data(
    tt_SiliconDevice& device, tt_multichip_core_addr sender_core, tt_multichip_core_addr receiver_core, uint32_t size);

// send one contiguous chunk of data to a vector of receiver cores
void broadcast_data(
    tt_SiliconDevice& device,
    tt_multichip_core_addr sender_core,
    std::vector<tt_multichip_core_addr> receiver_cores,
    uint32_t size);
