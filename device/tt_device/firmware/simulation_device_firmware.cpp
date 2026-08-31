// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/simulation_device_firmware.hpp"

namespace tt::umd {

void SimulationDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    wait_firmware_ready(timeout_ms, noc_id);
}

void SimulationDeviceFirmware::wait_firmware_ready(std::chrono::milliseconds timeout_ms, NocId noc_id) {}

DeviceCommandResult SimulationDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t> &args, std::chrono::milliseconds timeout, NocId noc_id) {
    return DeviceCommandResult{};
}

ChipInfo SimulationDeviceFirmware::get_chip_info(NocId noc_id) { return ChipInfo{}; }

bool SimulationDeviceFirmware::get_noc_translation_enabled(NocId noc_id) { return false; }

tt_xy_pair SimulationDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const { return tt_xy_pair{}; }

bool SimulationDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return false;
}

EthTrainingStatus SimulationDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    return EthTrainingStatus::NOT_CONNECTED;
}

bool SimulationDeviceFirmware::wait_dram_channel_training(
    uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return false;
}

DramTrainingStatus SimulationDeviceFirmware::get_dram_channel_training_status(uint32_t dram_channel, NocId noc_id) {
    return DramTrainingStatus::IN_PROGRESS;
}

void SimulationDeviceFirmware::set_power_state(PowerState state, NocId noc_id) {}

void SimulationDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {}

}  // namespace tt::umd
