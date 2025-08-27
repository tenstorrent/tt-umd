// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/firmware/wormhole_legacy_firmware_info_provider.h"

#include "umd/device/arc/smbus_arc_telemetry_reader.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/wormhole_dram.h"
#include "umd/device/types/wormhole_telemetry.h"

namespace tt::umd {

WormholeLegacyFirmwareInfoProvider::WormholeLegacyFirmwareInfoProvider(TTDevice* tt_device) :
    FirmwareInfoProvider(tt_device) {}

uint64_t WormholeLegacyFirmwareInfoProvider::get_board_id() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return ((uint64_t)telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_HIGH) << 32) |
           (telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_LOW));
}

uint32_t WormholeLegacyFirmwareInfoProvider::get_eth_fw_version() {
    return tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ETH_FW_VERSION);
}

double WormholeLegacyFirmwareInfoProvider::get_asic_temperature() {
    // Data stored in telemetry has temperature of ASIC stored in a way that high 16 bits
    // have integer part and lower 16 bits have fractional part.
    // It needs to be divided by 65536 to get temperature in Celsius.
    return (double)(tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ASIC_TEMPERATURE) &
                    0xFFFF) /
           8.0;
}

DramTrainingStatus WormholeLegacyFirmwareInfoProvider::get_dram_training_status(uint32_t dram_channel) {
    uint32_t telemetry_data = tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::DDR_STATUS);
    uint8_t status = (telemetry_data >> (dram_channel * 4)) & 0xF;

    switch (status) {
        case wormhole::WormholeDramTrainingStatus::TrainingNone:
            return DramTrainingStatus::IN_PROGRESS;
        case wormhole::WormholeDramTrainingStatus::TrainingFail:
            return DramTrainingStatus::FAIL;
        case wormhole::WormholeDramTrainingStatus::TrainingPass:
        case wormhole::WormholeDramTrainingStatus::TrainingSkip:
            return DramTrainingStatus::SUCCESS;
        default:
            return DramTrainingStatus::FAIL;
    }
}

}  // namespace tt::umd
