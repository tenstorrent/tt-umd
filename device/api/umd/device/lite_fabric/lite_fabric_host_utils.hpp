// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <vector>

#include "umd/device/lite_fabric/lite_fabric.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

namespace lite_fabric {

std::vector<uint8_t> read_binary_file(const std::string& file_name);

uint32_t get_eth_channel_mask(Chip* chip, const std::vector<CoreCoord>& eth_cores);

uint32_t get_local_init_addr();

void set_reset_state(Chip* chip, tt_cxy_pair translated_core, bool assert_reset);

void set_pc(Chip* chip, tt_cxy_pair translated_core, uint32_t pc_addr, uint32_t pc_val);

void wait_for_state(
    Chip* chip,
    tt_cxy_pair translated_core,
    uint32_t addr,
    uint32_t state,
    std::chrono::milliseconds timeout_ms = timeout::BH_LITE_FABRIC_STATE_CHANGE_TIMEOUT);

void launch_lite_fabric(Chip* chip, const std::vector<CoreCoord>& eth_cores);

void terminate_lite_fabric(Chip* chip, const std::vector<CoreCoord>& eth_cores);

}  // namespace lite_fabric

}  // namespace tt::umd
