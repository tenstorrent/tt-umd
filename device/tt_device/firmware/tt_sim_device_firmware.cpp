// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/tt_sim_device_firmware.hpp"

namespace tt::umd {

void TTSimDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {}

ChipInfo TTSimDeviceFirmware::get_chip_info(NocId noc_id) { return ChipInfo{}; }

bool TTSimDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return false;
}

EthTrainingStatus TTSimDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    return EthTrainingStatus::NOT_CONNECTED;
}

}  // namespace tt::umd
