// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/firmware/wormhole_18_3_firmware_info_provider.hpp"

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/wormhole_dram.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

Wormhole_18_3_FirmwareInfoProvider::Wormhole_18_3_FirmwareInfoProvider(TTDevice* tt_device) :
    Wormhole_18_7_FirmwareInfoProvider(tt_device) {}

uint64_t Wormhole_18_3_FirmwareInfoProvider::get_board_id() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return (static_cast<uint64_t>(telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_HIGH)) << 32) |
           (telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_LOW));
}

uint32_t Wormhole_18_3_FirmwareInfoProvider::get_eth_fw_version() {
    return tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ETH_FW_VERSION);
}

std::optional<double> Wormhole_18_3_FirmwareInfoProvider::get_asic_temperature() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (telemetry == nullptr || telemetry->is_entry_available(TelemetryTag::ASIC_TEMPERATURE)) {
        return std::nullopt;
    }
    // Data stored in telemetry has temperature of ASIC stored in a way that high 16 bits
    // have integer part and lower 16 bits have fractional part.
    // It needs to be divided by 65536 to get temperature in Celsius.
    return static_cast<double>((telemetry->read_entry(wormhole::TelemetryTag::ASIC_TEMPERATURE) & 0xFFFF)) / 8.0;
}

DramTrainingStatus Wormhole_18_3_FirmwareInfoProvider::get_dram_training_status(uint32_t dram_channel) {
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

uint32_t Wormhole_18_3_FirmwareInfoProvider::get_max_clock_freq() {
    uint32_t aiclk_telemetry = tt_device->get_arc_telemetry_reader()->read_entry(tt::umd::wormhole::AICLK);
    return (aiclk_telemetry >> 16) & 0xFFFF;
}

uint8_t Wormhole_18_3_FirmwareInfoProvider::get_asic_location() {
    // 0 is a placeholder value for older WH fw versions. This is something that is not used by SW for older Wormhole FW
    // versions.
    // TODO: support asic location to be optional if needed, at the moment this is not a priroty.
    return 0;
}

}  // namespace tt::umd
