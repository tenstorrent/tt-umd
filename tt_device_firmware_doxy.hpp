// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace tt::umd {

class DeviceFirmware {
public:
    virtual ~DeviceFirmware() = default;

    virtual void init_firmware(std::chrono::milliseconds timeout_ms) = 0;
    virtual DeviceCommandResult send_device_command(
        uint32_t msg_code, const std::vector<uint32_t> &args, std::chrono::milliseconds timeout) = 0;
    virtual bool get_noc_translation_enabled() = 0;
    virtual tt_xy_pair get_firmware_noc_coord() const = 0;
    virtual ChipInfo get_chip_info() = 0;
    virtual void set_power_state(uint32_t state) = 0;

    virtual bool wait_eth_core_training(tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms) = 0;
    virtual EthTrainingStatus get_eth_core_training_status(tt_xy_pair eth_core) = 0;
    virtual bool wait_dram_channel_training(uint32_t dram_channel, std::chrono::milliseconds timeout_ms) = 0;
    virtual DramTrainingStatus get_dram_channel_training_status(uint32_t dram_channel) = 0;
    virtual void set_clock_state(uint32_t state) = 0;
};

}  // namespace tt::umd
