// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/rtl_simulation_device_firmware.hpp"

namespace tt::umd {

void RtlSimulationDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    wait_firmware_ready(timeout_ms, noc_id);
}

bool RtlSimulationDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return false;
}

EthTrainingStatus RtlSimulationDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    return EthTrainingStatus::NOT_CONNECTED;
}

}  // namespace tt::umd
