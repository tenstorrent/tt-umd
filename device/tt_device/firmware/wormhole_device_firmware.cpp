// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"

#include <optional>

namespace tt::umd {

void WormholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {}

DeviceCommandResult WormholeDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t> &args, std::chrono::milliseconds timeout, NocId noc_id) {
    return DeviceCommandResult{};
}

ChipInfo WormholeDeviceFirmware::get_chip_info(NocId noc_id) { return ChipInfo{}; }

bool WormholeDeviceFirmware::get_noc_translation_enabled(NocId noc_id) { return false; }

tt_xy_pair WormholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const { return tt_xy_pair{}; }

bool WormholeDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return false;
}

EthTrainingStatus WormholeDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    return EthTrainingStatus::NOT_CONNECTED;
}

bool WormholeDeviceFirmware::wait_dram_channel_training(
    uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return false;
}

DramTrainingStatus WormholeDeviceFirmware::get_dram_channel_training_status(uint32_t dram_channel, NocId noc_id) {
    return DramTrainingStatus::IN_PROGRESS;
}

void WormholeDeviceFirmware::set_power_state(PowerState state, NocId noc_id) {}

void WormholeDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {}

std::optional<uint32_t> WormholeDeviceFirmware::get_runtime_telemetry_buffer_address(NocId noc_id) {
    return std::nullopt;
}

std::optional<uint32_t> WormholeDeviceFirmware::get_runtime_telemetry_buffer_size(NocId noc_id) { return std::nullopt; }

}  // namespace tt::umd
