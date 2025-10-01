// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/firmware/wormhole_18_3_firmware_info_provider.hpp"

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/wormhole_dram.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

Wormhole_18_3_FirmwareInfoProvider::Wormhole_18_3_FirmwareInfoProvider(TTDevice* tt_device) :
    Wormhole_18_7_FirmwareInfoProvider(tt_device) {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    aiclk_available = telemetry->is_entry_available(wormhole::TelemetryTag::AICLK);
    axiclk_available = telemetry->is_entry_available(wormhole::TelemetryTag::AXICLK);
    arcclk_available = telemetry->is_entry_available(wormhole::TelemetryTag::ARCCLK);
    fan_speed_available = telemetry->is_entry_available(wormhole::TelemetryTag::FAN_SPEED);
    tdp_available = telemetry->is_entry_available(wormhole::TelemetryTag::TDP);
    tdc_available = telemetry->is_entry_available(wormhole::TelemetryTag::TDC);
    vcore_available = telemetry->is_entry_available(wormhole::TelemetryTag::VCORE);
}

uint64_t Wormhole_18_3_FirmwareInfoProvider::get_board_id() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return (static_cast<uint64_t>(telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_HIGH)) << 32) |
           (telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_LOW));
}

uint32_t Wormhole_18_3_FirmwareInfoProvider::get_eth_fw_version() {
    return tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ETH_FW_VERSION);
}

double Wormhole_18_3_FirmwareInfoProvider::get_asic_temperature() {
    // Stored in S12.4 format.
    return static_cast<double>(
               (tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ASIC_TEMPERATURE) & 0xFFFF)) /
           16.0;
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

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_aiclk() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!aiclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::AICLK) & 0xFFFF;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_axiclk() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!axiclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::AXICLK);
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_arcclk() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!arcclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::ARCCLK);
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_fan_speed() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!fan_speed_available) {
        return std::nullopt;
    }
    const uint32_t fan_speed = telemetry->read_entry(wormhole::TelemetryTag::FAN_SPEED);
    // All ones mean fans not present on board, or not under control of firmware.
    if (fan_speed == 0xFFFFFFFF) {
        return std::nullopt;
    }
    return fan_speed;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_tdp() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!tdp_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::TDP) & 0xFFFF;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_tdc() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!tdc_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::TDC) & 0xFFFF;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_vcore() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!vcore_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::VCORE);
}

std::optional<double> Wormhole_18_3_FirmwareInfoProvider::get_board_temperature() {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!board_temperature_available) {
        return std::nullopt;
    }
    // Stored in s16.16 format. See Wormhole_18_3_FirmwareInfoProvider::get_asic_temperature()
    return static_cast<double>(telemetry->read_entry(wormhole::TelemetryTag::BOARD_TEMPERATURE)) / 65536.0f;
}

uint32_t Wormhole_18_3_FirmwareInfoProvider::get_heartbeat() {
    return tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ARC0_HEALTH);
}

}  // namespace tt::umd
