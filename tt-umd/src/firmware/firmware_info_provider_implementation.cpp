// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "common/unsupported.hpp"
#include "tt-umd/arc/firmware_telemetry_reader.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/telemetry.hpp"

namespace tt::umd {

namespace {

// Pass-through: the telemetry value is the feature's value.
constexpr LinearTransform IDENTITY{};

FeatureProfile standard(StandardTag tag, DataConverter converter = IDENTITY) { return {tag, converter}; }

}  // namespace

FirmwareFeatures FirmwareInfoProviderImplementation::create_18_4_new_telemetry_base() {
    return {
        {FirmwareFeature::BOARD_ID_HIGH, standard(TelemetryTag::BOARD_ID_HIGH)},
        {FirmwareFeature::BOARD_ID_LOW, standard(TelemetryTag::BOARD_ID_LOW)},
        {FirmwareFeature::ASIC_LOCATION, standard(TelemetryTag::ASIC_LOCATION)},
        // Signed 16.16 fixed point degrees Celsius.
        {FirmwareFeature::ASIC_TEMPERATURE,
         standard(
             TelemetryTag::ASIC_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0, NumericSign::SIGNED})},
        {FirmwareFeature::AICLK, standard(TelemetryTag::AICLK)},
        {FirmwareFeature::MAX_CLOCK_FREQ, standard(TelemetryTag::AICLK_LIMIT_MAX)},
    };
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_firmware_feature_map(
    tt::ARCH arch, const FirmwareBundleVersion& fw_version) {
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_NOT_SUPPORTED("Firmware telemetry of " + arch_to_str(arch));
    }
    return create_18_4_new_telemetry_base();
}

uint32_t FirmwareInfoProviderImplementation::read_raw_telemetry(const FeatureKey& key) const {
    if (const auto* fixed_value = std::get_if<FixedValue>(&key)) {
        return fixed_value->value;
    }
    if (const auto* tag = std::get_if<StandardTag>(&key)) {
        return telemetry_->read_entry(*tag);
    }
    UMD_NOT_SUPPORTED("Legacy Wormhole telemetry");
}

bool FirmwareInfoProviderImplementation::is_feature_available(FirmwareFeature feature) const {
    const auto profile = firmware_feature_map.find(feature);
    if (profile == firmware_feature_map.end() || std::holds_alternative<NotAvailable>(profile->second.converter)) {
        return false;
    }
    if (const auto* tag = std::get_if<StandardTag>(&profile->second.key)) {
        return telemetry_->is_entry_available(*tag);
    }
    return true;
}

template <typename T>
std::optional<T> FirmwareInfoProviderImplementation::read_scalar(FirmwareFeature feature) const {
    if (!is_feature_available(feature)) {
        return std::nullopt;
    }
    const FeatureProfile& profile = firmware_feature_map.at(feature);
    const auto& transform = std::get<LinearTransform>(profile.converter);
    const uint32_t field = (read_raw_telemetry(profile.key) >> transform.shift) & transform.mask;
    const double value = transform.signedness == NumericSign::SIGNED ? static_cast<double>(static_cast<int32_t>(field))
                                                                     : static_cast<double>(field);
    return static_cast<T>(value * transform.scale + transform.offset);
}

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProviderImplementation::create_firmware_info_provider(
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    FirmwareTelemetryReader* telemetry,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_NOT_SUPPORTED("Firmware telemetry of " + arch_to_str(arch));
    }
    const FirmwareBundleVersion firmware_version =
        FirmwareBundleVersion::from_firmware_bundle_tag(telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION));
    return std::make_unique<FirmwareInfoProviderImplementation>(
        firmware_version, arch, device_protocol, arc_core_noc0, arc_core_noc1, telemetry, legacy_telemetry_noc_addr);
}

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
    legacy_telemetry_noc_addr_(legacy_telemetry_noc_addr.value_or(0)),
    firmware_version(firmware_version),
    firmware_feature_map(create_firmware_feature_map(arch, firmware_version)) {}

FirmwareBundleVersion FirmwareInfoProviderImplementation::get_firmware_version(NocId noc_id) const {
    return firmware_version;
}

std::optional<uint64_t> FirmwareInfoProviderImplementation::get_board_id(NocId noc_id) const {
    const std::optional<uint32_t> board_id_high = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_HIGH);
    const std::optional<uint32_t> board_id_low = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_LOW);
    if (!board_id_high.has_value() || !board_id_low.has_value()) {
        return std::nullopt;
    }
    return (static_cast<uint64_t>(*board_id_high) << 32) | *board_id_low;
}

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
    return read_scalar<double>(FirmwareFeature::ASIC_TEMPERATURE);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_aiclk(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::AICLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_clock_freq(NocId noc_id) const {
    return get_aiclk(noc_id);
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
    return read_scalar<uint32_t>(FirmwareFeature::MAX_CLOCK_FREQ);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_min_clock_freq(NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::optional<uint8_t> FirmwareInfoProviderImplementation::get_asic_location(NocId noc_id) const {
    return read_scalar<uint8_t>(FirmwareFeature::ASIC_LOCATION);
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
