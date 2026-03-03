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

static bool gddr_telemetry_tags_available(TTDevice* tt_device) {
    return tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_2_3_TEMP)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_4_5_TEMP)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_6_7_TEMP)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_2_3_CORR_ERRS)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_4_5_CORR_ERRS)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_6_7_CORR_ERRS)) &&
           tt_device->get_arc_telemetry_reader()->is_entry_available(
               static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS));
}

std::optional<GddrModuleTelemetry> FirmwareInfoProvider::get_dram_telemetry(BlackholeGddr gddr_module) {
    // Telemetry data is packed in pairs: GDDR_0_1, GDDR_2_3, GDDR_4_5, GDDR_6_7.
    const uint8_t module_index = static_cast<uint8_t>(gddr_module);
    const uint8_t pair_index = module_index / 2;  // Which pair: 0, 1, 2, or 3.
    const bool is_odd_module = (module_index % 2) == 1;
    const uint8_t bit_shift = is_odd_module ? 16 : 0;  // Odd modules use upper 16 bits.

    // Read the packed telemetry word for this pair.
    const uint32_t temperature_word = tt_device->get_arc_telemetry_reader()->read_entry(
        static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) + pair_index);
    const uint32_t corrected_errors_word = tt_device->get_arc_telemetry_reader()->read_entry(
        static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) + pair_index);
    const uint32_t uncorrected_errors_bitmask =
        tt_device->get_arc_telemetry_reader()->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS));

    // Extract this module's data from the packed word.
    // Layout per module: [15:8] top temp / write errors, [7:0] bottom temp / read errors.
    GddrModuleTelemetry telemetry{};
    telemetry.dram_temperature_bottom = static_cast<uint8_t>((temperature_word >> bit_shift) & 0xFFu);
    telemetry.dram_temperature_top = static_cast<uint8_t>((temperature_word >> (bit_shift + 8)) & 0xFFu);
    telemetry.corr_edc_rd_errors = static_cast<uint8_t>((corrected_errors_word >> bit_shift) & 0xFFu);
    telemetry.corr_edc_wr_errors = static_cast<uint8_t>((corrected_errors_word >> (bit_shift + 8)) & 0xFFu);

    // Uncorrected errors: 2 bits per module (bit i*2 = read, bit i*2+1 = write).
    const uint8_t uncorr_read_bit = module_index * 2;
    const uint8_t uncorr_write_bit = module_index * 2 + 1;
    telemetry.uncorr_edc_rd_error = (uncorrected_errors_bitmask & (1u << uncorr_read_bit)) != 0 ? 1 : 0;
    telemetry.uncorr_edc_wr_error = (uncorrected_errors_bitmask & (1u << uncorr_write_bit)) != 0 ? 1 : 0;

    return telemetry;
}

std::optional<GddrTelemetry> FirmwareInfoProvider::get_aggregated_dram_telemetry() {
    if (!gddr_telemetry_tags_available(tt_device)) {
        return std::nullopt;
    }

    GddrTelemetry aggregated_gddr_telemetry{};

    auto uncorrected_errors_mask =
        tt_device->get_arc_telemetry_reader()->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS));

    const std::array<uint8_t, 4> temp_tags = {
        static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP),
        static_cast<uint8_t>(TelemetryTag::GDDR_2_3_TEMP),
        static_cast<uint8_t>(TelemetryTag::GDDR_4_5_TEMP),
        static_cast<uint8_t>(TelemetryTag::GDDR_6_7_TEMP),
    };
    const std::array<uint8_t, 4> corr_tags = {
        static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS),
        static_cast<uint8_t>(TelemetryTag::GDDR_2_3_CORR_ERRS),
        static_cast<uint8_t>(TelemetryTag::GDDR_4_5_CORR_ERRS),
        static_cast<uint8_t>(TelemetryTag::GDDR_6_7_CORR_ERRS),
    };

    for (std::size_t gddr_index_pair = 0; gddr_index_pair < 4; ++gddr_index_pair) {
        const uint32_t temp_word = tt_device->get_arc_telemetry_reader()->read_entry(temp_tags[gddr_index_pair]);
        const uint32_t corr_word = tt_device->get_arc_telemetry_reader()->read_entry(corr_tags[gddr_index_pair]);
        const std::size_t base = gddr_index_pair * 2;
        const BlackholeGddr gddr_x = static_cast<BlackholeGddr>(base);
        const BlackholeGddr gddr_y = static_cast<BlackholeGddr>(base + 1);

        // Layaggregated_gddr_telemetry: [31:24] y top, [23:16] y bottom, [15:8] x top, [7:0] x bottom.
        aggregated_gddr_telemetry.modules[gddr_x].dram_temperature_bottom = static_cast<uint8_t>(temp_word & 0xFFu);
        aggregated_gddr_telemetry.modules[gddr_x].dram_temperature_top = static_cast<uint8_t>((temp_word >> 8) & 0xFFu);
        aggregated_gddr_telemetry.modules[gddr_y].dram_temperature_bottom =
            static_cast<uint8_t>((temp_word >> 16) & 0xFFu);
        aggregated_gddr_telemetry.modules[gddr_y].dram_temperature_top =
            static_cast<uint8_t>((temp_word >> 24) & 0xFFu);

        // Layaggregated_gddr_telemetry: [31:24] y corr write, [23:16] y corr read, [15:8] x corr write, [7:0] x corr
        // read.
        aggregated_gddr_telemetry.modules[gddr_x].corr_edc_rd_errors = static_cast<uint8_t>(corr_word & 0xFFu);
        aggregated_gddr_telemetry.modules[gddr_x].corr_edc_wr_errors = static_cast<uint8_t>((corr_word >> 8) & 0xFFu);
        aggregated_gddr_telemetry.modules[gddr_y].corr_edc_rd_errors = static_cast<uint8_t>((corr_word >> 16) & 0xFFu);
        aggregated_gddr_telemetry.modules[gddr_y].corr_edc_wr_errors = static_cast<uint8_t>((corr_word >> 24) & 0xFFu);
    }

    for (std::size_t i = 0; i < NUM_GDDR_MODULES; ++i) {
        BlackholeGddr gddr = static_cast<BlackholeGddr>(i);
        aggregated_gddr_telemetry.modules[gddr].uncorr_edc_rd_error =
            (uncorrected_errors_mask & (1u << (i * 2))) != 0 ? 1 : 0;
        aggregated_gddr_telemetry.modules[gddr].uncorr_edc_wr_error =
            (uncorrected_errors_mask & (1u << (i * 2 + 1))) != 0 ? 1 : 0;
    }

    return aggregated_gddr_telemetry;
}

uint16_t FirmwareInfoProvider::get_dram_speed() {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::GDDR_SPEED);
}

uint16_t FirmwareInfoProvider::get_current_max_dram_temperature() {
    return tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::MAX_GDDR_TEMP);
}

}  // namespace tt::umd
