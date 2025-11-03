/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>

#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "umd/device/cluster.hpp"
#include "umd/device/types/xy_pair.hpp"

// static const std::string SOC_DESC_PATH = "./tests/soc_descs/wormhole_b0_8x10.yaml";

using namespace tt;
using namespace tt::umd;

struct tt_multichip_core_addr {
    tt_multichip_core_addr() : core{}, chip{}, addr{} {}

    tt_multichip_core_addr(ChipId chip, CoreCoord core, std::uint64_t addr) : core(core), chip(chip), addr(addr) {}

    CoreCoord core;
    ChipId chip;
    std::uint64_t addr;

    std::string str() const { return fmt::format("(chip={},core={},addr=0x{:x})", chip, core.str(), addr); }
};

// SIMPLE DATAMOVEMENT API BASED ON UMD
// send one contiguous chunk of data from one sender core to a receiver core
void move_data(
    Cluster& device, tt_multichip_core_addr sender_core, tt_multichip_core_addr receiver_core, uint32_t size);

// send one contiguous chunk of data to a vector of receiver cores
void broadcast_data(
    Cluster& device,
    tt_multichip_core_addr sender_core,
    std::vector<tt_multichip_core_addr> receiver_cores,
    uint32_t size);
