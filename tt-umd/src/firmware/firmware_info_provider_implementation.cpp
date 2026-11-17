// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"

#include "tt-umd/arc/smbus_arc_telemetry_reader.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

FirmwareInfoProviderImplementation::FirmwareInfoProviderImplementation(
    FirmwareBundleVersion firmware_version,
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    FirmwareTelemetryReader* telemetry,
    std::optional<uint64_t> legacy_telemetry_noc_addr) :
    arch_(arch),
    device_protocol_(device_protocol),
    telemetry_(telemetry),
    arc_core_noc0_(arc_core_noc0),
    arc_core_noc1_(arc_core_noc1),
    legacy_telemetry_noc_addr_(legacy_telemetry_noc_addr.value_or(SmBusArcTelemetryReader::DEFAULT_TELEMETRY_NOC_ADDR)),
    firmware_version(firmware_version) {}

FirmwareBundleVersion FirmwareInfoProviderImplementation::get_firmware_version(NocId noc_id) const {
    return firmware_version;
}

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProviderImplementation::create_firmware_info_provider(
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    FirmwareTelemetryReader* telemetry,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    UMD_THROW(
        error::RuntimeError,
        "FirmwareInfoProviderImplementation::create_firmware_info_provider is not implemented yet");
}

std::optional<uint64_t> FirmwareInfoProviderImplementation::get_board_id(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_board_id is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_eth_fw_version(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_eth_fw_version is not implemented yet");
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_eth_fw_version_semver(NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::get_eth_fw_version_semver is not implemented yet");
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_gddr_fw_version(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_gddr_fw_version is not implemented yet");
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_cm_fw_version(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_cm_fw_version is not implemented yet");
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_app_fw_version(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_dm_app_fw_version is not implemented yet");
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_bl_fw_version(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_dm_bl_fw_version is not implemented yet");
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_tt_flash_version(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_tt_flash_version is not implemented yet");
}

std::optional<double> FirmwareInfoProviderImplementation::get_asic_temperature(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_asic_temperature is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_aiclk(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_aiclk is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_clock_freq(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_clock_freq is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_axiclk(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_axiclk is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_arcclk(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_arcclk is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_speed(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_fan_speed is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_rpm(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_fan_rpm is not implemented yet");
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_speeds(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_fan_speeds is not implemented yet");
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_rpms(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_fan_rpms is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_tdp is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdc(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_tdc is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_vcore(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_vcore is not implemented yet");
}

std::optional<double> FirmwareInfoProviderImplementation::get_board_temperature(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_board_temperature is not implemented yet");
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_shutdown(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_thm_limit_shutdown is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp_limit(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_tdp_limit is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_board_power_limit(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_board_power_limit is not implemented yet");
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_throttle(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_thm_limit_throttle is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_therm_trip_count(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_therm_trip_count is not implemented yet");
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_heartbeat_status(NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::get_eth_heartbeat_status is not implemented yet");
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_retrain_status(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_eth_retrain_status is not implemented yet");
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_heartbeat_status_per_core(NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError,
        "FirmwareInfoProviderImplementation::get_eth_heartbeat_status_per_core is not implemented yet");
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_retrain_status_per_core(NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError,
        "FirmwareInfoProviderImplementation::get_eth_retrain_status_per_core is not implemented yet");
}

std::optional<std::vector<std::pair<CoreCoord, bool>>> FirmwareInfoProviderImplementation::get_eth_link_status_per_core(
    NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::get_eth_link_status_per_core is not implemented yet");
}

std::vector<DramTrainingStatus> FirmwareInfoProviderImplementation::get_dram_training_status(
    uint32_t num_dram_channels, NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::get_dram_training_status is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_max_clock_freq(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_max_clock_freq is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_min_clock_freq(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_min_clock_freq is not implemented yet");
}

std::optional<uint8_t> FirmwareInfoProviderImplementation::get_asic_location(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_asic_location is not implemented yet");
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_heartbeat(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_heartbeat is not implemented yet");
}

std::optional<GddrTelemetry> FirmwareInfoProviderImplementation::get_aggregated_dram_telemetry(NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError,
        "FirmwareInfoProviderImplementation::get_aggregated_dram_telemetry is not implemented yet");
}

std::optional<GddrModuleTelemetry> FirmwareInfoProviderImplementation::get_dram_telemetry(
    GddrModule gddr_module, NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_dram_telemetry is not implemented yet");
}

std::optional<uint16_t> FirmwareInfoProviderImplementation::get_dram_speed(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::get_dram_speed is not implemented yet");
}

std::optional<double> FirmwareInfoProviderImplementation::get_current_max_dram_temperature(NocId noc_id) const {
    UMD_THROW(
        error::RuntimeError,
        "FirmwareInfoProviderImplementation::get_current_max_dram_temperature is not implemented yet");
}

std::vector<std::pair<CoreCoord, bool>> FirmwareInfoProviderImplementation::parse_eth_status_bitmask(
    uint16_t bitmask) const {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::parse_eth_status_bitmask is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_firmware_feature_map(
    tt::ARCH arch, const FirmwareBundleVersion& fw_version) {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_firmware_feature_map is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_18_4_new_telemetry_base() {
    UMD_THROW(
        error::RuntimeError,
        "FirmwareInfoProviderImplementation::create_18_4_new_telemetry_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_18_3_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_wormhole_18_3_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_18_4_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_wormhole_18_4_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_5_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_blackhole_18_5_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_18_8_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_wormhole_18_8_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_19_9_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_wormhole_19_9_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_8_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_blackhole_18_8_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_8_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_blackhole_19_8_base is not implemented yet");
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_9_base() {
    UMD_THROW(
        error::RuntimeError, "FirmwareInfoProviderImplementation::create_blackhole_19_9_base is not implemented yet");
}

uint32_t FirmwareInfoProviderImplementation::read_raw_telemetry(const FeatureKey& key) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::read_raw_telemetry is not implemented yet");
}

bool FirmwareInfoProviderImplementation::is_feature_available(FirmwareFeature feature) const {
    UMD_THROW(error::RuntimeError, "FirmwareInfoProviderImplementation::is_feature_available is not implemented yet");
}

}  // namespace tt::umd
