// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/tt_sim_device_firmware.hpp"

namespace tt::umd {

TTSimDeviceFirmware::TTSimDeviceFirmware(tt::ARCH arch) : arch_(arch) {}

void TTSimDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    wait_firmware_ready(timeout_ms, noc_id);
}

ChipInfo TTSimDeviceFirmware::get_chip_info(NocId noc_id) {
    // There is no FirmwareInfoProvider on the simulator, so the defaults mirror the ones
    // TTSimTTDevice::create() uses. Blackhole SocDescriptor construction rejects an empty
    // eth_harvesting_mask ("Exactly 2 or 14 ETH cores should be harvested on full Blackhole"), so the
    // same 0x120 default is applied here. Keep in sync with create().
    ChipInfo chip_info{};
    if (arch_ == tt::ARCH::BLACKHOLE) {
        chip_info.harvesting_masks.eth_harvesting_mask = 0x120;
    }
    return chip_info;
}

bool TTSimDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    return false;
}

EthTrainingStatus TTSimDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    return EthTrainingStatus::NOT_CONNECTED;
}

}  // namespace tt::umd
