// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "common/unsupported.hpp"
#include "tt-umd/arc/firmware_telemetry_reader.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/telemetry.hpp"

namespace tt::umd {

namespace {

// Pass-through: the telemetry value is the feature's value.
constexpr LinearTransform IDENTITY{};

// Signed 16.16 fixed point degrees Celsius.
constexpr LinearTransform FIXED_POINT_16_16_CELSIUS{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0, NumericSign::SIGNED};

// ETH_LIVE_STATUS holds one bit per ETH channel for the heartbeat in its low half and for the link in its high half.
constexpr LinearTransform ETH_HEARTBEAT_BITS{0, 0xFFFF};
constexpr LinearTransform ETH_LINK_BITS{16, 0xFFFF};

FeatureProfile standard(StandardTag tag, DataConverter converter = IDENTITY) { return {tag, converter}; }

const FeatureProfile NOT_AVAILABLE{FixedValue{0}, NotAvailable{}};

// Fan telemetry holds one 16-bit field per fan, all ones for a fan the firmware does not report.
constexpr uint32_t FAN_FIELD_BITS = 16;
constexpr uint32_t FAN_FIELD_MASK = 0xFFFF;

// GDDR telemetry tags each cover a pair of modules, 16 bits per module: temperatures (top die, then bottom die) or
// corrected error counts (read, then write), one byte each.
constexpr size_t GDDR_MODULES_PER_TAG = 2;
constexpr uint32_t GDDR_MODULE_FIELD_BITS = 16;
constexpr FirmwareFeature GDDR_TEMP_FEATURES[] = {
    FirmwareFeature::GDDR_0_1_TEMP,
    FirmwareFeature::GDDR_2_3_TEMP,
    FirmwareFeature::GDDR_4_5_TEMP,
    FirmwareFeature::GDDR_6_7_TEMP};
constexpr FirmwareFeature GDDR_CORR_ERRS_FEATURES[] = {
    FirmwareFeature::GDDR_0_1_CORR_ERRS,
    FirmwareFeature::GDDR_2_3_CORR_ERRS,
    FirmwareFeature::GDDR_4_5_CORR_ERRS,
    FirmwareFeature::GDDR_6_7_CORR_ERRS};
// GDDR_UNCORR_ERRS holds two bits per module: an uncorrected read error, then an uncorrected write error.
constexpr uint32_t GDDR_UNCORR_ERRS_BITS = 2;

// GDDR_STATUS holds two bits per DRAM channel: training done, then training failed.
constexpr uint32_t DDR_STATUS_BITS = 2;
constexpr uint64_t DDR_TRAINING_DONE = 1u << 0;
constexpr uint64_t DDR_TRAINING_FAILED = 1u << 1;

uint8_t byte_at(uint32_t value, uint32_t byte_index) { return static_cast<uint8_t>(value >> (8 * byte_index)); }

std::vector<std::optional<uint32_t>> split_fan_fields(std::optional<uint32_t> fans, size_t num_fans) {
    std::vector<std::optional<uint32_t>> per_fan(num_fans);
    for (size_t fan = 0; fan < num_fans && fans.has_value(); fan++) {
        const uint32_t field = (*fans >> (fan * FAN_FIELD_BITS)) & FAN_FIELD_MASK;
        if (field != FAN_FIELD_MASK) {
            per_fan[fan] = field;
        }
    }
    return per_fan;
}

std::optional<std::vector<bool>> drop_cores(const std::optional<std::vector<std::pair<CoreCoord, bool>>>& per_core) {
    if (!per_core.has_value()) {
        return std::nullopt;
    }
    std::vector<bool> statuses;
    for (const auto& [eth_core, status] : *per_core) {
        statuses.push_back(status);
    }
    return statuses;
}

}  // namespace

FirmwareFeatures FirmwareInfoProviderImplementation::create_18_4_new_telemetry_base() {
    return {
        {FirmwareFeature::BOARD_ID_HIGH, standard(TelemetryTag::BOARD_ID_HIGH)},
        {FirmwareFeature::BOARD_ID_LOW, standard(TelemetryTag::BOARD_ID_LOW)},
        {FirmwareFeature::ASIC_LOCATION, standard(TelemetryTag::ASIC_LOCATION)},
        {FirmwareFeature::ASIC_TEMPERATURE, standard(TelemetryTag::ASIC_TEMPERATURE, FIXED_POINT_16_16_CELSIUS)},
        {FirmwareFeature::BOARD_TEMPERATURE, standard(TelemetryTag::BOARD_TEMPERATURE, FIXED_POINT_16_16_CELSIUS)},
        {FirmwareFeature::AICLK, standard(TelemetryTag::AICLK)},
        {FirmwareFeature::AXICLK, standard(TelemetryTag::AXICLK)},
        {FirmwareFeature::ARCCLK, standard(TelemetryTag::ARCCLK)},
        {FirmwareFeature::VCORE, standard(TelemetryTag::VCORE)},
        {FirmwareFeature::TDP, standard(TelemetryTag::TDP)},
        {FirmwareFeature::TDC, standard(TelemetryTag::TDC)},
        {FirmwareFeature::TDP_LIMIT_MAX, standard(TelemetryTag::TDP_LIMIT_MAX)},
        {FirmwareFeature::BOARD_POWER_LIMIT, standard(TelemetryTag::BOARD_POWER_LIMIT)},
        // Thermal limits are whole degrees Celsius.
        {FirmwareFeature::THM_LIMIT_SHUTDOWN, standard(TelemetryTag::THM_LIMIT_SHUTDOWN)},
        {FirmwareFeature::THM_LIMIT_THROTTLE, standard(TelemetryTag::THM_LIMIT_THROTTLE)},
        {FirmwareFeature::FAN_SPEED, standard(TelemetryTag::FAN_SPEED)},
        {FirmwareFeature::FAN_RPM, standard(TelemetryTag::FAN_RPM)},
        {FirmwareFeature::HEARTBEAT, standard(TelemetryTag::TIMER_HEARTBEAT)},
        {FirmwareFeature::DDR_STATUS, standard(TelemetryTag::GDDR_STATUS)},
        {FirmwareFeature::DDR_SPEED, standard(TelemetryTag::GDDR_SPEED)},
        {FirmwareFeature::ETH_FW_VERSION, standard(TelemetryTag::ETH_FW_VERSION)},
        {FirmwareFeature::GDDR_FW_VERSION, standard(TelemetryTag::GDDR_FW_VERSION)},
        {FirmwareFeature::CM_FW_VERSION, standard(TelemetryTag::CM_FW_VERSION)},
        {FirmwareFeature::DM_APP_FW_VERSION, standard(TelemetryTag::DM_APP_FW_VERSION)},
        {FirmwareFeature::DM_BL_FW_VERSION, standard(TelemetryTag::DM_BL_FW_VERSION)},
        {FirmwareFeature::TT_FLASH_VERSION, standard(TelemetryTag::TT_FLASH_VERSION)},
    };
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_5_base() {
    FirmwareFeatures features = create_18_4_new_telemetry_base();
    // ETH firmware reports its version in each ETH core's L1 (get_eth_fw_version()), not through ARC telemetry.
    features[FirmwareFeature::ETH_FW_VERSION] = NOT_AVAILABLE;
    // Firmware before 19.8 cannot change the TDP limit, so it publishes none.
    features[FirmwareFeature::TDP_LIMIT_MAX] = NOT_AVAILABLE;
    // Firmware before 18.8 publishes no AICLK limit; it runs busy at the architecture's busy clock.
    features[FirmwareFeature::MAX_CLOCK_FREQ] = {FixedValue{blackhole::AICLK_BUSY_VAL}, IDENTITY};
    features[FirmwareFeature::THERM_TRIP_COUNT] = standard(TelemetryTag::THERM_TRIP_COUNT);
    features[FirmwareFeature::GDDR_0_1_TEMP] = standard(TelemetryTag::GDDR_0_1_TEMP);
    features[FirmwareFeature::GDDR_2_3_TEMP] = standard(TelemetryTag::GDDR_2_3_TEMP);
    features[FirmwareFeature::GDDR_4_5_TEMP] = standard(TelemetryTag::GDDR_4_5_TEMP);
    features[FirmwareFeature::GDDR_6_7_TEMP] = standard(TelemetryTag::GDDR_6_7_TEMP);
    features[FirmwareFeature::MAX_GDDR_TEMP] = standard(TelemetryTag::MAX_GDDR_TEMP);
    features[FirmwareFeature::GDDR_0_1_CORR_ERRS] = standard(TelemetryTag::GDDR_0_1_CORR_ERRS);
    features[FirmwareFeature::GDDR_2_3_CORR_ERRS] = standard(TelemetryTag::GDDR_2_3_CORR_ERRS);
    features[FirmwareFeature::GDDR_4_5_CORR_ERRS] = standard(TelemetryTag::GDDR_4_5_CORR_ERRS);
    features[FirmwareFeature::GDDR_6_7_CORR_ERRS] = standard(TelemetryTag::GDDR_6_7_CORR_ERRS);
    features[FirmwareFeature::GDDR_UNCORR_ERRS] = standard(TelemetryTag::GDDR_UNCORR_ERRS);
    return features;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_8_base() {
    FirmwareFeatures features = create_blackhole_18_5_base();
    features[FirmwareFeature::MAX_CLOCK_FREQ] = standard(TelemetryTag::AICLK_LIMIT_MAX);
    return features;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_8_base() {
    FirmwareFeatures features = create_blackhole_18_8_base();
    features[FirmwareFeature::TDP_LIMIT_MAX] = standard(TelemetryTag::TDP_LIMIT_MAX);
    return features;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_9_base() {
    FirmwareFeatures features = create_blackhole_19_8_base();
    features[FirmwareFeature::ETH_HEARTBEAT_STATUS] = standard(TelemetryTag::ETH_LIVE_STATUS, ETH_HEARTBEAT_BITS);
    features[FirmwareFeature::ETH_LINK_STATUS] = standard(TelemetryTag::ETH_LIVE_STATUS, ETH_LINK_BITS);
    return features;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_firmware_feature_map(
    tt::ARCH arch, const FirmwareBundleVersion& fw_version) {
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_NOT_SUPPORTED("Firmware telemetry of " + arch_to_str(arch));
    }
    if (fw_version >= FirmwareBundleVersion(19, 9, 0)) {
        return create_blackhole_19_9_base();
    }
    if (fw_version >= FirmwareBundleVersion(19, 8, 0)) {
        return create_blackhole_19_8_base();
    }
    if (fw_version >= FirmwareBundleVersion(18, 8, 0)) {
        return create_blackhole_18_8_base();
    }
    return create_blackhole_18_5_base();
}

// The engine reads over the calling thread's NOC; each getter selects the NOC it was asked to route through.
uint32_t FirmwareInfoProviderImplementation::read_raw_telemetry(const FeatureKey& key) const {
    if (const auto* fixed_value = std::get_if<FixedValue>(&key)) {
        return fixed_value->value;
    }
    if (const auto* tag = std::get_if<StandardTag>(&key)) {
        return telemetry_->read_entry(*tag, get_selected_noc_id());
    }
    UMD_NOT_SUPPORTED("Legacy Wormhole telemetry");
}

bool FirmwareInfoProviderImplementation::is_feature_available(FirmwareFeature feature) const {
    const auto profile = firmware_feature_map.find(feature);
    if (profile == firmware_feature_map.end() || std::holds_alternative<NotAvailable>(profile->second.converter)) {
        return false;
    }
    if (const auto* tag = std::get_if<StandardTag>(&profile->second.key)) {
        return telemetry_->is_entry_available(*tag, get_selected_noc_id());
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
    const FirmwareBundleVersion firmware_version = FirmwareBundleVersion::from_firmware_bundle_tag(
        telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION, get_selected_noc_id()));
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
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> board_id_high = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_HIGH);
    const std::optional<uint32_t> board_id_low = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_LOW);
    if (!board_id_high.has_value() || !board_id_low.has_value()) {
        return std::nullopt;
    }
    return (static_cast<uint64_t>(*board_id_high) << 32) | *board_id_low;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_eth_fw_version(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_eth_fw_version_semver(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> version = read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
    if (!version.has_value()) {
        return std::nullopt;
    }
    return arch_ == tt::ARCH::WORMHOLE_B0 ? SemVer::from_wormhole_eth_firmware_tag(*version)
                                          : SemVer::from_blackhole_eth_firmware_tag(*version);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_gddr_fw_version(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> version = read_scalar<uint32_t>(FirmwareFeature::GDDR_FW_VERSION);
    if (!version.has_value()) {
        return std::nullopt;
    }
    return get_gddr_fw_version_from_telemetry(*version, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_cm_fw_version(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> version = read_scalar<uint32_t>(FirmwareFeature::CM_FW_VERSION);
    if (!version.has_value()) {
        return std::nullopt;
    }
    return get_cm_fw_version_from_telemetry(*version, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_app_fw_version(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> version = read_scalar<uint32_t>(FirmwareFeature::DM_APP_FW_VERSION);
    if (!version.has_value()) {
        return std::nullopt;
    }
    return get_dm_app_fw_version_from_telemetry(*version, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_bl_fw_version(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> version = read_scalar<uint32_t>(FirmwareFeature::DM_BL_FW_VERSION);
    if (!version.has_value()) {
        return std::nullopt;
    }
    return get_dm_bl_fw_version_from_telemetry(*version, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_tt_flash_version(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> version = read_scalar<uint32_t>(FirmwareFeature::TT_FLASH_VERSION);
    if (!version.has_value()) {
        return std::nullopt;
    }
    return get_tt_flash_version_from_telemetry(*version);
}

std::optional<double> FirmwareInfoProviderImplementation::get_asic_temperature(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::ASIC_TEMPERATURE);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_aiclk(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::AICLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_clock_freq(NocId noc_id) const {
    return get_aiclk(noc_id);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_axiclk(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::AXICLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_arcclk(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::ARCCLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_speed(NocId noc_id) const {
    return get_fan_speeds(noc_id).front();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_rpm(NocId noc_id) const {
    return get_fan_rpms(noc_id).front();
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_speeds(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return split_fan_fields(read_scalar<uint32_t>(FirmwareFeature::FAN_SPEED), MAX_NUMBER_OF_FANS);
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_rpms(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return split_fan_fields(read_scalar<uint32_t>(FirmwareFeature::FAN_RPM), MAX_NUMBER_OF_FANS);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::TDP);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdc(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::TDC);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_vcore(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::VCORE);
}

std::optional<double> FirmwareInfoProviderImplementation::get_board_temperature(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::BOARD_TEMPERATURE);
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_shutdown(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_SHUTDOWN);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp_limit(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::TDP_LIMIT_MAX);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_board_power_limit(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::BOARD_POWER_LIMIT);
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_throttle(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_THROTTLE);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_therm_trip_count(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::THERM_TRIP_COUNT);
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_heartbeat_status(NocId noc_id) const {
    return drop_cores(get_eth_heartbeat_status_per_core(noc_id));
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_retrain_status(NocId noc_id) const {
    return drop_cores(get_eth_retrain_status_per_core(noc_id));
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_heartbeat_status_per_core(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint16_t> bitmask = read_scalar<uint16_t>(FirmwareFeature::ETH_HEARTBEAT_STATUS);
    if (!bitmask.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(*bitmask);
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_retrain_status_per_core(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint16_t> bitmask = read_scalar<uint16_t>(FirmwareFeature::ETH_RETRAIN_STATUS);
    if (!bitmask.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(*bitmask);
}

std::optional<std::vector<std::pair<CoreCoord, bool>>> FirmwareInfoProviderImplementation::get_eth_link_status_per_core(
    NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint16_t> bitmask = read_scalar<uint16_t>(FirmwareFeature::ETH_LINK_STATUS);
    if (!bitmask.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(*bitmask);
}

std::vector<std::pair<CoreCoord, bool>> FirmwareInfoProviderImplementation::parse_eth_status_bitmask(
    uint16_t bitmask) const {
    // Bit i belongs to ETH channel i, which the architecture lists in channel order.
    std::vector<std::pair<CoreCoord, bool>> statuses;
    for (size_t channel = 0; channel < blackhole::ETH_CORES_NOC0.size(); channel++) {
        const tt_xy_pair eth_core = blackhole::ETH_CORES_NOC0[channel];
        statuses.emplace_back(
            CoreCoord(eth_core.x, eth_core.y, CoreType::ETH, CoordSystem::NOC0), ((bitmask >> channel) & 1) != 0);
    }
    return statuses;
}

std::vector<DramTrainingStatus> FirmwareInfoProviderImplementation::get_dram_training_status(
    uint32_t num_dram_channels, NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> ddr_status = read_scalar<uint32_t>(FirmwareFeature::DDR_STATUS);
    if (!ddr_status.has_value()) {
        return {};
    }
    std::vector<DramTrainingStatus> statuses;
    for (uint32_t channel = 0; channel < num_dram_channels; channel++) {
        // Widened so channels past the field's end read as still training instead of shifting out of range.
        const uint64_t channel_status = static_cast<uint64_t>(*ddr_status) >> (channel * DDR_STATUS_BITS);
        if ((channel_status & DDR_TRAINING_FAILED) != 0) {
            statuses.push_back(DramTrainingStatus::FAIL);
        } else if ((channel_status & DDR_TRAINING_DONE) != 0) {
            statuses.push_back(DramTrainingStatus::SUCCESS);
        } else {
            statuses.push_back(DramTrainingStatus::IN_PROGRESS);
        }
    }
    return statuses;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_max_clock_freq(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::MAX_CLOCK_FREQ);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_min_clock_freq(NocId noc_id) const {
    // Firmware publishes no minimum; its power management drops AICLK to the architecture's idle clock.
    return blackhole::AICLK_IDLE_VAL;
}

std::optional<uint8_t> FirmwareInfoProviderImplementation::get_asic_location(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint8_t>(FirmwareFeature::ASIC_LOCATION);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_heartbeat(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::HEARTBEAT);
}

std::optional<GddrTelemetry> FirmwareInfoProviderImplementation::get_aggregated_dram_telemetry(NocId noc_id) const {
    GddrTelemetry telemetry;
    for (size_t module = 0; module < get_number_of_dram_modules(arch_); module++) {
        const GddrModule gddr_module = static_cast<GddrModule>(module);
        std::optional<GddrModuleTelemetry> module_telemetry = get_dram_telemetry(gddr_module, noc_id);
        if (!module_telemetry.has_value()) {
            return std::nullopt;
        }
        telemetry.modules.emplace(gddr_module, *module_telemetry);
    }
    return telemetry;
}

std::optional<GddrModuleTelemetry> FirmwareInfoProviderImplementation::get_dram_telemetry(
    GddrModule gddr_module, NocId noc_id) const {
    const size_t module = static_cast<size_t>(gddr_module);
    if (module >= get_number_of_dram_modules(arch_)) {
        return std::nullopt;
    }
    const NocIdSwitcher noc_switcher(noc_id);
    const size_t tag_index = module / GDDR_MODULES_PER_TAG;
    const std::optional<uint32_t> temperatures = read_scalar<uint32_t>(GDDR_TEMP_FEATURES[tag_index]);
    const std::optional<uint32_t> corrected_errors = read_scalar<uint32_t>(GDDR_CORR_ERRS_FEATURES[tag_index]);
    const std::optional<uint32_t> uncorrected_errors = read_scalar<uint32_t>(FirmwareFeature::GDDR_UNCORR_ERRS);
    if (!temperatures.has_value() || !corrected_errors.has_value() || !uncorrected_errors.has_value()) {
        return std::nullopt;
    }

    const uint32_t field_shift = (module % GDDR_MODULES_PER_TAG) * GDDR_MODULE_FIELD_BITS;
    const uint32_t temperature_field = *temperatures >> field_shift;
    const uint32_t corrected_field = *corrected_errors >> field_shift;
    const uint32_t uncorrected_field = *uncorrected_errors >> (module * GDDR_UNCORR_ERRS_BITS);
    GddrModuleTelemetry module_telemetry;
    module_telemetry.dram_temperature_top = byte_at(temperature_field, 0);
    module_telemetry.dram_temperature_bottom = byte_at(temperature_field, 1);
    module_telemetry.corr_edc_rd_errors = byte_at(corrected_field, 0);
    module_telemetry.corr_edc_wr_errors = byte_at(corrected_field, 1);
    module_telemetry.uncorr_edc_rd_error = uncorrected_field & 1;
    module_telemetry.uncorr_edc_wr_error = (uncorrected_field >> 1) & 1;
    return module_telemetry;
}

std::optional<uint16_t> FirmwareInfoProviderImplementation::get_dram_speed(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint16_t>(FirmwareFeature::DDR_SPEED);
}

std::optional<double> FirmwareInfoProviderImplementation::get_current_max_dram_temperature(NocId noc_id) const {
    const NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::MAX_GDDR_TEMP);
}

}  // namespace tt::umd
