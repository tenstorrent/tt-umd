// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/wormhole_18_3_firmware_info_provider.hpp"

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/wormhole_dram.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

Wormhole_18_3_FirmwareInfoProvider::Wormhole_18_3_FirmwareInfoProvider(TTDevice* tt_device, bool use_noc1) :
    Wormhole_18_7_FirmwareInfoProvider(tt_device, use_noc1) {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    aiclk_available = telemetry->is_entry_available(wormhole::TelemetryTag::AICLK);
    axiclk_available = telemetry->is_entry_available(wormhole::TelemetryTag::AXICLK);
    arcclk_available = telemetry->is_entry_available(wormhole::TelemetryTag::ARCCLK);
    fan_speed_available = telemetry->is_entry_available(wormhole::TelemetryTag::FAN_SPEED);
    tdp_available = telemetry->is_entry_available(wormhole::TelemetryTag::TDP);
    tdc_available = telemetry->is_entry_available(wormhole::TelemetryTag::TDC);
    vcore_available = telemetry->is_entry_available(wormhole::TelemetryTag::VCORE);
}

uint64_t Wormhole_18_3_FirmwareInfoProvider::get_board_id(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return (static_cast<uint64_t>(telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_HIGH, use_noc1)) << 32) |
           (telemetry->read_entry(wormhole::TelemetryTag::BOARD_ID_LOW, use_noc1));
}

uint32_t Wormhole_18_3_FirmwareInfoProvider::get_eth_fw_version(bool use_noc1) const {
    return tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ETH_FW_VERSION, use_noc1);
}

std::optional<semver_t> Wormhole_18_3_FirmwareInfoProvider::get_eth_fw_version_semver(bool use_noc1) const {
    return get_eth_fw_version_from_telemetry(
        tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ETH_FW_VERSION, use_noc1),
        tt_device->get_arch());
}

double Wormhole_18_3_FirmwareInfoProvider::get_asic_temperature(bool use_noc1) const {
    // Stored in S12.4 format.
    return static_cast<double>(
               (tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ASIC_TEMPERATURE, use_noc1) &
                0xFFFF)) /
           16.0;
}

std::vector<DramTrainingStatus> Wormhole_18_3_FirmwareInfoProvider::get_dram_training_status(
    uint32_t num_dram_channels, bool use_noc1) const {
    uint32_t telemetry_data =
        tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::DDR_STATUS, use_noc1);

    // Each dram channel uses 4 bits in the 32-bit value in order to represent the state of DRAM training.
    // That's why we move by 4 bits for each channel to get its status.
    std::vector<DramTrainingStatus> statuses;
    for (uint32_t dram_channel = 0; dram_channel < num_dram_channels; ++dram_channel) {
        uint8_t status = (telemetry_data >> (dram_channel * 4)) & 0xF;

        switch (status) {
            case wormhole::WormholeDramTrainingStatus::TrainingNone:
                statuses.push_back(DramTrainingStatus::IN_PROGRESS);
                break;
            case wormhole::WormholeDramTrainingStatus::TrainingFail:
                statuses.push_back(DramTrainingStatus::FAIL);
                break;
            case wormhole::WormholeDramTrainingStatus::TrainingPass:
            case wormhole::WormholeDramTrainingStatus::TrainingSkip:
                statuses.push_back(DramTrainingStatus::SUCCESS);
                break;
            default:
                statuses.push_back(DramTrainingStatus::FAIL);
        }
    }

    return statuses;
}

uint32_t Wormhole_18_3_FirmwareInfoProvider::get_max_clock_freq(bool use_noc1) const {
    uint32_t aiclk_telemetry = tt_device->get_arc_telemetry_reader()->read_entry(wormhole::AICLK, use_noc1);
    return (aiclk_telemetry >> 16) & 0xFFFF;
}

uint8_t Wormhole_18_3_FirmwareInfoProvider::get_asic_location(bool use_noc1) const {
    // 0 is a placeholder value for older WH fw versions. This is something that is not used by SW for older Wormhole FW
    // versions.
    // TODO: support asic location to be optional if needed, at the moment this is not a priroty.
    return 0;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_aiclk(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!aiclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::AICLK, use_noc1) & 0xFFFF;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_axiclk(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!axiclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::AXICLK, use_noc1);
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_arcclk(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!arcclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::ARCCLK, use_noc1);
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_fan_speed(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!fan_speed_available) {
        return std::nullopt;
    }
    const uint32_t fan_speed = telemetry->read_entry(wormhole::TelemetryTag::FAN_SPEED, use_noc1);
    // All ones mean fans not present on board, or not under control of firmware.
    if (fan_speed == 0xFFFFFFFF) {
        return std::nullopt;
    }
    return fan_speed;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_tdp(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!tdp_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::TDP, use_noc1) & 0xFFFF;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_tdc(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!tdc_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::TDC, use_noc1) & 0xFFFF;
}

std::optional<uint32_t> Wormhole_18_3_FirmwareInfoProvider::get_vcore(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!vcore_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(wormhole::TelemetryTag::VCORE, use_noc1);
}

std::optional<double> Wormhole_18_3_FirmwareInfoProvider::get_board_temperature(bool use_noc1) const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!board_temperature_available) {
        return std::nullopt;
    }
    // Stored in s16.16 format. See Wormhole_18_3_FirmwareInfoProvider::get_asic_temperature().
    return static_cast<double>(telemetry->read_entry(wormhole::TelemetryTag::BOARD_TEMPERATURE, use_noc1)) / 65536.0f;
}

uint32_t Wormhole_18_3_FirmwareInfoProvider::get_heartbeat(bool use_noc1) const {
    return tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::ARC0_HEALTH, use_noc1);
}

std::optional<semver_t> Wormhole_18_3_FirmwareInfoProvider::get_gddr_fw_version(bool use_noc1) const {
    // Seems like GDDR FW version is not available in Wormhole 18.3.x firmware.
    return std::nullopt;
}

std::optional<semver_t> Wormhole_18_3_FirmwareInfoProvider::get_cm_fw_version(bool use_noc1) const {
    // Seems like CM FW version is not available in Wormhole 18.3.x firmware.
    return std::nullopt;
}

std::optional<semver_t> Wormhole_18_3_FirmwareInfoProvider::get_dm_app_fw_version(bool use_noc1) const {
    return get_dm_app_fw_version_from_telemetry(
        tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::DM_APP_FW_VERSION, use_noc1),
        tt::ARCH::WORMHOLE_B0);
}

std::optional<semver_t> Wormhole_18_3_FirmwareInfoProvider::get_dm_bl_fw_version(bool use_noc1) const {
    return get_dm_bl_fw_version_from_telemetry(
        tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::DM_BL_FW_VERSION, use_noc1),
        tt::ARCH::WORMHOLE_B0);
}

std::optional<semver_t> Wormhole_18_3_FirmwareInfoProvider::get_tt_flash_version(bool use_noc1) const {
    return get_tt_flash_version_from_telemetry(
        tt_device->get_arc_telemetry_reader()->read_entry(wormhole::TelemetryTag::TT_FLASH_VERSION, use_noc1));
}

}  // namespace tt::umd
