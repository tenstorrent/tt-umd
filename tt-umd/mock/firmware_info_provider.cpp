// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_info_provider.hpp"

namespace tt::umd {

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProvider::create_firmware_info_provider(TTDevice* tt_device) {
    return std::make_unique<FirmwareInfoProvider>(tt_device);
}

FirmwareInfoProvider::FirmwareInfoProvider(TTDevice* tt_device) : tt_device(tt_device) {}

FirmwareBundleVersion FirmwareInfoProvider::get_firmware_version() const { return FirmwareBundleVersion{}; }

FirmwareBundleVersion FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH) {
    return FirmwareBundleVersion{};
}

FirmwareBundleVersion FirmwareInfoProvider::get_latest_supported_firmware_version(tt::ARCH) {
    return FirmwareBundleVersion{};
}

uint64_t FirmwareInfoProvider::get_board_id() const { return 0; }

std::optional<uint32_t> FirmwareInfoProvider::get_eth_fw_version() const { return std::nullopt; }

std::optional<SemVer> FirmwareInfoProvider::get_eth_fw_version_semver() const { return std::nullopt; }

std::optional<SemVer> FirmwareInfoProvider::get_gddr_fw_version() const { return std::nullopt; }

std::optional<SemVer> FirmwareInfoProvider::get_cm_fw_version() const { return std::nullopt; }

std::optional<SemVer> FirmwareInfoProvider::get_dm_app_fw_version() const { return std::nullopt; }

std::optional<SemVer> FirmwareInfoProvider::get_dm_bl_fw_version() const { return std::nullopt; }

std::optional<SemVer> FirmwareInfoProvider::get_tt_flash_version() const { return std::nullopt; }

double FirmwareInfoProvider::get_asic_temperature() const { return 0.0; }

std::optional<uint32_t> FirmwareInfoProvider::get_aiclk() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_axiclk() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_arcclk() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_fan_speed() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_fan_rpm() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_tdp() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_tdc() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_vcore() const { return std::nullopt; }

std::optional<double> FirmwareInfoProvider::get_board_temperature() const { return std::nullopt; }

std::optional<double> FirmwareInfoProvider::get_thm_limit_shutdown() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_board_power_limit() const { return std::nullopt; }

std::optional<double> FirmwareInfoProvider::get_thm_limit_throttle() const { return std::nullopt; }

std::optional<uint32_t> FirmwareInfoProvider::get_therm_trip_count() const { return std::nullopt; }

std::optional<std::vector<bool>> FirmwareInfoProvider::get_eth_heartbeat_status() const { return std::nullopt; }

std::optional<std::vector<bool>> FirmwareInfoProvider::get_eth_retrain_status() const { return std::nullopt; }

std::vector<DramTrainingStatus> FirmwareInfoProvider::get_dram_training_status(uint32_t) const { return {}; }

uint32_t FirmwareInfoProvider::get_max_clock_freq() const { return 0; }

uint8_t FirmwareInfoProvider::get_asic_location() const { return 0; }

uint32_t FirmwareInfoProvider::get_heartbeat() const { return 0; }

std::optional<GddrTelemetry> FirmwareInfoProvider::get_aggregated_dram_telemetry() const { return std::nullopt; }

std::optional<GddrModuleTelemetry> FirmwareInfoProvider::get_dram_telemetry(GddrModule) const { return std::nullopt; }

std::optional<uint16_t> FirmwareInfoProvider::get_dram_speed() const { return std::nullopt; }

std::optional<double> FirmwareInfoProvider::get_current_max_dram_temperature() const { return std::nullopt; }

}  // namespace tt::umd
