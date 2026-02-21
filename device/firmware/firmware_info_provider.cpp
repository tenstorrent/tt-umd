// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/firmware_info_provider.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/firmware/blackhole_18_7_firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/firmware/wormhole_18_3_firmware_info_provider.hpp"
#include "umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

FirmwareInfoProvider::FirmwareInfoProvider(TTDevice* tt_device) :
    tt_device(tt_device), firmware_version(get_firmware_version_util(tt_device)) {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (telemetry == nullptr) {
        throw std::runtime_error("No telemetry reader present in tt_device.");
    }

    aiclk_available = telemetry->is_entry_available(TelemetryTag::AICLK);
    axiclk_available = telemetry->is_entry_available(TelemetryTag::AXICLK);
    arcclk_available = telemetry->is_entry_available(TelemetryTag::ARCCLK);
    fan_speed_available = telemetry->is_entry_available(TelemetryTag::FAN_SPEED);
    tdp_available = telemetry->is_entry_available(TelemetryTag::TDP);
    tdc_available = telemetry->is_entry_available(TelemetryTag::TDC);
    vcore_available = telemetry->is_entry_available(TelemetryTag::VCORE);
    board_temperature_available = telemetry->is_entry_available(TelemetryTag::BOARD_TEMPERATURE);
}

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProvider::create_firmware_info_provider(TTDevice* tt_device) {
    FirmwareBundleVersion fw_bundle_version = get_firmware_version_util(tt_device);
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0: {
            if (fw_bundle_version > FirmwareBundleVersion(18, 7, 0)) {
                return std::make_unique<FirmwareInfoProvider>(tt_device);
            }

            if (fw_bundle_version > FirmwareBundleVersion(18, 3, 0)) {
                return std::make_unique<Wormhole_18_7_FirmwareInfoProvider>(tt_device);
            }

            return std::make_unique<Wormhole_18_3_FirmwareInfoProvider>(tt_device);
        }
        case ARCH::BLACKHOLE: {
            if (fw_bundle_version > FirmwareBundleVersion(18, 7, 0)) {
                return std::make_unique<FirmwareInfoProvider>(tt_device);
            }

            return std::make_unique<Blackhole_18_7_FirmwareInfoProvider>(tt_device);
        }
        default:
            throw std::runtime_error("Unsupported architecture for firmware versioner.");
    }
}

FirmwareBundleVersion FirmwareInfoProvider::get_firmware_version() const { return firmware_version; }

FirmwareBundleVersion FirmwareInfoProvider::get_latest_supported_firmware_version(tt::ARCH arch) {
    return FirmwareBundleVersion(19, 4, 0);
}

FirmwareBundleVersion FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            return FirmwareBundleVersion(0, 0, 0);
        }
        case tt::ARCH::BLACKHOLE: {
            return FirmwareBundleVersion(18, 5, 0);
        }
        default:
            throw std::runtime_error("Unsupported architecture for firmware info provider.");
    }
}

uint64_t FirmwareInfoProvider::get_board_id() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return (static_cast<uint64_t>(telemetry->read_entry(TelemetryTag::BOARD_ID_HIGH)) << 32) |
           (telemetry->read_entry(TelemetryTag::BOARD_ID_LOW));
}

uint32_t FirmwareInfoProvider::get_eth_fw_version() const {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ETH_FW_VERSION);
}

std::optional<SemVer> FirmwareInfoProvider::get_eth_fw_version_semver() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::ETH_FW_VERSION)) {
        return std::nullopt;
    }
    switch (tt_device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            return SemVer::from_wormhole_eth_firmware_tag(get_eth_fw_version());
        default:  // ETH FW version is not reported in ARC telemetry for Blackhole.
            return std::nullopt;
    }
}

std::optional<SemVer> FirmwareInfoProvider::get_gddr_fw_version() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::GDDR_FW_VERSION)) {
        return std::nullopt;
    }
    return get_gddr_fw_version_from_telemetry(
        telemetry->read_entry(TelemetryTag::GDDR_FW_VERSION), tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_cm_fw_version() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::CM_FW_VERSION)) {
        return std::nullopt;
    }
    return get_cm_fw_version_from_telemetry(telemetry->read_entry(TelemetryTag::CM_FW_VERSION), tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_dm_app_fw_version() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::DM_APP_FW_VERSION)) {
        return std::nullopt;
    }
    return get_dm_app_fw_version_from_telemetry(
        telemetry->read_entry(TelemetryTag::DM_APP_FW_VERSION), tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_dm_bl_fw_version() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::DM_BL_FW_VERSION)) {
        return std::nullopt;
    }
    return get_dm_bl_fw_version_from_telemetry(
        telemetry->read_entry(TelemetryTag::DM_BL_FW_VERSION), tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_tt_flash_version() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::TT_FLASH_VERSION)) {
        return std::nullopt;
    }
    return get_tt_flash_version_from_telemetry(telemetry->read_entry(TelemetryTag::TT_FLASH_VERSION));
}

std::vector<DramTrainingStatus> FirmwareInfoProvider::get_dram_training_status(uint32_t num_dram_channels) const {
    // Format of the dram training status is as follows:
    // Each channel gets two bits in the 32-bit value (16 bits used). The lower bits are for lower channels.
    // Lower of the two bits reports the training error and higher of the two bits reports the training status.
    // Example: 0b 00 00 00 00 00 00 01 10
    // would mean that only channel 0 is trained, channel 1 has the error and other channels are not trained and don't
    // have errors. If some channel is harvested the bits are always going to be zero.
    uint32_t telemetry_data = tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::DDR_STATUS);
    std::vector<DramTrainingStatus> statuses;
    for (uint32_t channel = 0; channel < num_dram_channels; ++channel) {
        if (telemetry_data & (1 << (2 * channel))) {
            statuses.push_back(DramTrainingStatus::SUCCESS);
        } else if (telemetry_data & (1 << (2 * channel + 1))) {
            statuses.push_back(DramTrainingStatus::FAIL);
        } else {
            statuses.push_back(DramTrainingStatus::IN_PROGRESS);
        }
    }
    return statuses;
}

uint32_t FirmwareInfoProvider::get_max_clock_freq() const {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::AICLK_LIMIT_MAX);
}

uint8_t FirmwareInfoProvider::get_asic_location() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return telemetry->is_entry_available(TelemetryTag::ASIC_LOCATION)
               ? static_cast<uint8_t>(telemetry->read_entry(TelemetryTag::ASIC_LOCATION))
               : 0;
}

double FirmwareInfoProvider::get_asic_temperature() const {
    // Data stored in telemetry has temperature of ASIC stored in a way that high 16 bits
    // have integer part and lower 16 bits have fractional part.
    // It needs to be divided by 65536 to get temperature in Celsius.
    return static_cast<double>(tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ASIC_TEMPERATURE)) /
           65536.0f;
}

std::optional<uint32_t> FirmwareInfoProvider::get_aiclk() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!aiclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::AICLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_axiclk() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!axiclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::AXICLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_arcclk() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!arcclk_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::ARCCLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_fan_speed() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!fan_speed_available) {
        return std::nullopt;
    }
    const uint32_t fan_speed = telemetry->read_entry(TelemetryTag::FAN_SPEED);
    // All ones mean fans not present on board, or not under control of firmware.
    if (fan_speed == 0xFFFFFFFF) {
        return std::nullopt;
    }
    return fan_speed;
}

std::optional<uint32_t> FirmwareInfoProvider::get_tdp() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!tdp_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::TDP);
}

std::optional<uint32_t> FirmwareInfoProvider::get_tdc() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!tdc_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::TDC);
}

std::optional<uint32_t> FirmwareInfoProvider::get_vcore() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!vcore_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::VCORE);
}

std::optional<double> FirmwareInfoProvider::get_board_temperature() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!board_temperature_available) {
        return std::nullopt;
    }
    // Stored in s16.16 format. See FirmwareInfoProvider::get_asic_temperature().
    return static_cast<double>(telemetry->read_entry(TelemetryTag::BOARD_TEMPERATURE)) / 65536.0f;
}

uint32_t FirmwareInfoProvider::get_heartbeat() const {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::TIMER_HEARTBEAT);
}

}  // namespace tt::umd
