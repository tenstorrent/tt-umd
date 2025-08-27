// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/firmware/wormhole_18_4_firmware_info_provider.h"

#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/wormhole_dram.h"
#include "umd/device/types/wormhole_telemetry.h"

namespace tt::umd {

Wormhole_18_4_FirmwareInfoProvider::Wormhole_18_4_FirmwareInfoProvider(TTDevice* tt_device) :
    WormholeLegacyFirmwareInfoProvider(tt_device) {}

uint64_t Wormhole_18_4_FirmwareInfoProvider::get_board_id() { return FirmwareInfoProvider::get_board_id(); }

uint32_t Wormhole_18_4_FirmwareInfoProvider::get_eth_fw_version() { return FirmwareInfoProvider::get_eth_fw_version(); }

double Wormhole_18_4_FirmwareInfoProvider::get_asic_temperature() {
    return FirmwareInfoProvider::get_asic_temperature();
}

DramTrainingStatus Wormhole_18_4_FirmwareInfoProvider::get_dram_training_status(uint32_t dram_channel) {
    return FirmwareInfoProvider::get_dram_training_status(dram_channel);
}

}  // namespace tt::umd
