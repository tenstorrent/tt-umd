// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProviderImplementation::create_firmware_info_provider(
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    FirmwareTelemetryReader* telemetry,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    UMD_NOT_IMPLEMENTED();
}

FirmwareInfoProviderImplementation::FirmwareInfoProviderImplementation(
    FirmwareBundleVersion firmware_version,
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    FirmwareTelemetryReader* telemetry,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    UMD_NOT_IMPLEMENTED();
}

FirmwareBundleVersion FirmwareInfoProviderImplementation::get_firmware_version(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint64_t> FirmwareInfoProviderImplementation::get_board_id(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_eth_fw_version(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_eth_fw_version_semver(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_gddr_fw_version(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_cm_fw_version(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_app_fw_version(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_bl_fw_version(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_tt_flash_version(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<double> FirmwareInfoProviderImplementation::get_asic_temperature(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_aiclk(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_clock_freq(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_axiclk(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_arcclk(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_speed(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_rpm(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_speeds(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_rpms(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdc(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_vcore(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<double> FirmwareInfoProviderImplementation::get_board_temperature(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_shutdown(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp_limit(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_board_power_limit(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_throttle(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_therm_trip_count(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_heartbeat_status(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_retrain_status(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_heartbeat_status_per_core(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_retrain_status_per_core(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<std::vector<std::pair<CoreCoord, bool>>> FirmwareInfoProviderImplementation::get_eth_link_status_per_core(
    NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<DramTrainingStatus> FirmwareInfoProviderImplementation::get_dram_training_status(
    uint32_t num_dram_channels, NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_max_clock_freq(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_min_clock_freq(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint8_t> FirmwareInfoProviderImplementation::get_asic_location(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_heartbeat(NocId noc_id) const { UMD_NOT_IMPLEMENTED(); }

std::optional<GddrTelemetry> FirmwareInfoProviderImplementation::get_aggregated_dram_telemetry(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<GddrModuleTelemetry> FirmwareInfoProviderImplementation::get_dram_telemetry(
    GddrModule gddr_module, NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint16_t> FirmwareInfoProviderImplementation::get_dram_speed(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<double> FirmwareInfoProviderImplementation::get_current_max_dram_temperature(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
