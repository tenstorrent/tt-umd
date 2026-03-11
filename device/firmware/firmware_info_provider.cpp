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
#include "umd/device/types/gddr_telemetry.hpp"
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
    thm_limit_shutdown_available = telemetry->is_entry_available(TelemetryTag::THM_LIMIT_SHUTDOWN);
    board_power_limit_available = telemetry->is_entry_available(TelemetryTag::BOARD_POWER_LIMIT);
    thm_limit_throttle_available = telemetry->is_entry_available(TelemetryTag::THM_LIMIT_THROTTLE);
    therm_trip_count_available = telemetry->is_entry_available(TelemetryTag::THERM_TRIP_COUNT);
    // ETH_LIVE_STATUS is not implemented for Blackhole; the tag exists but always returns zeros.
    eth_live_status_available =
        telemetry->is_entry_available(TelemetryTag::ETH_LIVE_STATUS) && tt_device->get_arch() != tt::ARCH::BLACKHOLE;
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
    return FirmwareBundleVersion(19, 5, 0);
}

FirmwareBundleVersion FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            return FirmwareBundleVersion(18, 3, 0);
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
    uint32_t telemetry_data = tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::GDDR_STATUS);
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
    // Stored in signed 16.16 fixed-point format (s16.16): high 16 bits are the integer part,
    // lower 16 bits are the fractional part. Cast through int32_t to preserve the sign bit.
    return static_cast<double>(static_cast<int32_t>(
               tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ASIC_TEMPERATURE))) /
           65536.0;
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
    // Stored in signed 16.16 fixed-point format (s16.16). See get_asic_temperature().
    return static_cast<double>(static_cast<int32_t>(telemetry->read_entry(TelemetryTag::BOARD_TEMPERATURE))) / 65536.0;
}

uint32_t FirmwareInfoProvider::get_heartbeat() const {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::TIMER_HEARTBEAT);
}

static bool gddr_telemetry_tags_available(TTDevice* tt_device) {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    // All GDDR telemetry tags from GDDR_0_1_TEMP through MAX_GDDR_TEMP must be present.
    for (uint8_t tag = static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP);
         tag <= static_cast<uint8_t>(TelemetryTag::MAX_GDDR_TEMP);
         ++tag) {
        if (!telemetry->is_entry_available(tag)) {
            return false;
        }
    }
    return true;
}

// Ensure GDDR telemetry tags are consecutive so pair_index arithmetic is safe.
static_assert(
    static_cast<uint8_t>(TelemetryTag::GDDR_6_7_TEMP) - static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) == 3,
    "GDDR_x_y_TEMP tags must be consecutive");
static_assert(
    static_cast<uint8_t>(TelemetryTag::GDDR_6_7_CORR_ERRS) - static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) ==
        3,
    "GDDR_x_y_CORR_ERRS tags must be consecutive");

// Decode a single GDDR module's telemetry from the packed pair words.
// Telemetry packs two modules per 32-bit word.  Even modules (0,2,4,6) occupy bits [15:0],
// odd modules (1,3,5,7) occupy bits [31:16].  Within each half:
//   [7:0]  = bottom temp / read errors     [15:8] = top temp / write errors
// Uncorrected errors use a separate bitmask: bit module_index*2 = read, bit module_index*2+1 = write.
static GddrModuleTelemetry decode_gddr_module_telemetry(
    uint8_t module_index, uint32_t temp_word, uint32_t corr_word, uint32_t uncorr_bitmask) {
    const uint8_t bit_shift = (module_index % 2 == 1) ? 16 : 0;

    GddrModuleTelemetry module{};
    module.dram_temperature_bottom = static_cast<double>((temp_word >> bit_shift) & 0xFFu);
    module.dram_temperature_top = static_cast<double>((temp_word >> (bit_shift + 8)) & 0xFFu);
    module.corr_edc_rd_errors = static_cast<uint8_t>((corr_word >> bit_shift) & 0xFFu);
    module.corr_edc_wr_errors = static_cast<uint8_t>((corr_word >> (bit_shift + 8)) & 0xFFu);
    module.uncorr_edc_rd_error = (uncorr_bitmask & (1u << (module_index * 2))) != 0 ? 1 : 0;
    module.uncorr_edc_wr_error = (uncorr_bitmask & (1u << (module_index * 2 + 1))) != 0 ? 1 : 0;
    return module;
}

std::optional<GddrModuleTelemetry> FirmwareInfoProvider::get_dram_telemetry(GddrModule gddr_module) const {
    const uint8_t module_index = static_cast<uint8_t>(gddr_module);
    const uint8_t pair_index = module_index / 2;

    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();

    if (!telemetry->is_entry_available(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) + pair_index) ||
        !telemetry->is_entry_available(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) + pair_index) ||
        !telemetry->is_entry_available(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS))) {
        return std::nullopt;
    }

    const uint32_t temp_word = telemetry->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) + pair_index);
    const uint32_t corr_word =
        telemetry->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) + pair_index);
    const uint32_t uncorr_bitmask = telemetry->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS));

    return decode_gddr_module_telemetry(module_index, temp_word, corr_word, uncorr_bitmask);
}

std::optional<GddrTelemetry> FirmwareInfoProvider::get_aggregated_dram_telemetry() const {
    if (!gddr_telemetry_tags_available(tt_device)) {
        return std::nullopt;
    }

    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    GddrTelemetry aggregated{};

    const uint32_t uncorr_bitmask = telemetry->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS));

    for (uint8_t pair = 0; pair < 4; ++pair) {
        const uint32_t temp_word = telemetry->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) + pair);
        const uint32_t corr_word = telemetry->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) + pair);

        const uint8_t even_module = pair * 2;
        const uint8_t odd_module = pair * 2 + 1;

        aggregated.modules[static_cast<GddrModule>(even_module)] =
            decode_gddr_module_telemetry(even_module, temp_word, corr_word, uncorr_bitmask);
        aggregated.modules[static_cast<GddrModule>(odd_module)] =
            decode_gddr_module_telemetry(odd_module, temp_word, corr_word, uncorr_bitmask);
    }

    return aggregated;
}

std::optional<uint16_t> FirmwareInfoProvider::get_dram_speed() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::GDDR_SPEED)) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(telemetry->read_entry(TelemetryTag::GDDR_SPEED));
}

std::optional<double> FirmwareInfoProvider::get_current_max_dram_temperature() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!telemetry->is_entry_available(TelemetryTag::MAX_GDDR_TEMP)) {
        return std::nullopt;
    }
    return static_cast<double>(telemetry->read_entry(TelemetryTag::MAX_GDDR_TEMP));
}

std::optional<double> FirmwareInfoProvider::get_thm_limit_shutdown() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!thm_limit_shutdown_available) {
        return std::nullopt;
    }
    // Stored as a plain integer in degrees Celsius.
    return static_cast<double>(telemetry->read_entry(TelemetryTag::THM_LIMIT_SHUTDOWN));
}

std::optional<uint32_t> FirmwareInfoProvider::get_board_power_limit() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!board_power_limit_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::BOARD_POWER_LIMIT);
}

std::optional<double> FirmwareInfoProvider::get_thm_limit_throttle() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!thm_limit_throttle_available) {
        return std::nullopt;
    }
    // Stored as a plain integer in degrees Celsius.
    return static_cast<double>(telemetry->read_entry(TelemetryTag::THM_LIMIT_THROTTLE));
}

std::optional<uint32_t> FirmwareInfoProvider::get_therm_trip_count() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!therm_trip_count_available) {
        return std::nullopt;
    }
    return telemetry->read_entry(TelemetryTag::THERM_TRIP_COUNT);
}

std::vector<bool> FirmwareInfoProvider::parse_eth_status_bitmask(uint16_t bitmask) {
    static constexpr uint32_t max_eth_links = 16;
    std::vector<bool> statuses(max_eth_links);
    for (uint32_t link = 0; link < max_eth_links; ++link) {
        statuses[link] = static_cast<bool>(bitmask & (1u << link));
    }
    return statuses;
}

std::optional<std::vector<bool>> FirmwareInfoProvider::get_eth_heartbeat_status() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!eth_live_status_available) {
        return std::nullopt;
    }
    uint32_t data = telemetry->read_entry(TelemetryTag::ETH_LIVE_STATUS);
    return parse_eth_status_bitmask(static_cast<uint16_t>(data & 0xFFFF));
}

std::optional<std::vector<bool>> FirmwareInfoProvider::get_eth_retrain_status() const {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (!eth_live_status_available) {
        return std::nullopt;
    }
    uint32_t data = telemetry->read_entry(TelemetryTag::ETH_LIVE_STATUS);
    return parse_eth_status_bitmask(static_cast<uint16_t>((data >> 16) & 0xFFFF));
}

}  // namespace tt::umd
