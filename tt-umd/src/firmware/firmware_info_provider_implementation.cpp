// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "tt-umd/arc/firmware_telemetry_reader.hpp"
#include "tt-umd/arc/smbus_arc_telemetry_reader.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Firmware marks fan telemetry with this value when the board has no fan control.
constexpr uint32_t FAN_NOT_CONTROLLED = 0xFFFFFFFF;

const FirmwareBundleVersion FW_VERSION_18_8(18, 8, 0);
const FirmwareBundleVersion FW_VERSION_19_8(19, 8, 0);
const FirmwareBundleVersion FW_VERSION_19_9(19, 9, 0);

FeatureProfile tag(StandardTag tag, LinearTransform transform = {}) { return FeatureProfile{tag, transform}; }

FeatureProfile not_available() { return FeatureProfile{FixedValue{0}, NotAvailable{}}; }

// Temperatures are signed 16.16 fixed point.
constexpr LinearTransform S16_16{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0, NumericSign::SIGNED};

// Decodes one GDDR module from the packed words that hold it and its pair: temperatures and corrected
// error counts carry the even module of a pair in the low half-word, and the uncorrected error word
// has a read and a write bit per module.
GddrModuleTelemetry decode_gddr_module(
    GddrModule module, uint32_t temp_word, uint32_t corr_word, uint32_t uncorr_word) {
    const uint32_t index = static_cast<uint32_t>(module);
    const uint32_t pair_shift = (index % 2) * 16;
    GddrModuleTelemetry telemetry;
    telemetry.dram_temperature_bottom = static_cast<double>((temp_word >> pair_shift) & 0xFF);
    telemetry.dram_temperature_top = static_cast<double>((temp_word >> (pair_shift + 8)) & 0xFF);
    telemetry.corr_edc_rd_errors = static_cast<uint8_t>((corr_word >> pair_shift) & 0xFF);
    telemetry.corr_edc_wr_errors = static_cast<uint8_t>((corr_word >> (pair_shift + 8)) & 0xFF);
    telemetry.uncorr_edc_rd_error = static_cast<uint8_t>((uncorr_word >> (2 * index)) & 1);
    telemetry.uncorr_edc_wr_error = static_cast<uint8_t>((uncorr_word >> (2 * index + 1)) & 1);
    return telemetry;
}

FirmwareFeature gddr_temp_feature(GddrModule module) {
    static constexpr FirmwareFeature features[] = {
        FirmwareFeature::GDDR_0_1_TEMP,
        FirmwareFeature::GDDR_2_3_TEMP,
        FirmwareFeature::GDDR_4_5_TEMP,
        FirmwareFeature::GDDR_6_7_TEMP};
    return features[static_cast<size_t>(module) / 2];
}

FirmwareFeature gddr_corr_errs_feature(GddrModule module) {
    static constexpr FirmwareFeature features[] = {
        FirmwareFeature::GDDR_0_1_CORR_ERRS,
        FirmwareFeature::GDDR_2_3_CORR_ERRS,
        FirmwareFeature::GDDR_4_5_CORR_ERRS,
        FirmwareFeature::GDDR_6_7_CORR_ERRS};
    return features[static_cast<size_t>(module) / 2];
}

}  // namespace

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
    firmware_version(firmware_version),
    firmware_feature_map(create_firmware_feature_map(arch, firmware_version)) {}

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProviderImplementation::create_firmware_info_provider(
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    FirmwareTelemetryReader* telemetry,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    UMD_ASSERT(
        arch == tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        fmt::format("Firmware information is not supported on {}.", arch_to_str(arch)));

    const FirmwareBundleVersion firmware_version =
        FirmwareBundleVersion::from_firmware_bundle_tag(telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION));
    log_debug(tt::LogUMD, "Firmware bundle version {}.", firmware_version.to_string());

    const FirmwareBundleVersion minimum_version = get_minimum_compatible_firmware_version(arch);
    const FirmwareBundleVersion latest_version = get_latest_supported_firmware_version(arch);
    if (firmware_version < minimum_version) {
        log_warning(
            tt::LogUMD,
            "Firmware {} is older than the oldest firmware UMD supports on {} ({}); firmware information may be "
            "wrong. Update the firmware.",
            firmware_version.to_string(),
            arch_to_str(arch),
            minimum_version.to_string());
    } else if (firmware_version > latest_version) {
        log_warning(
            tt::LogUMD,
            "Firmware {} is newer than the latest firmware this UMD was tested with on {} ({}); update UMD if "
            "firmware information looks wrong.",
            firmware_version.to_string(),
            arch_to_str(arch),
            latest_version.to_string());
    }

    return std::make_unique<FirmwareInfoProviderImplementation>(
        firmware_version, arch, device_protocol, arc_core_noc0, arc_core_noc1, telemetry, legacy_telemetry_noc_addr);
}

FirmwareBundleVersion FirmwareInfoProviderImplementation::get_firmware_version(NocId noc_id) const {
    return firmware_version;
}

std::optional<uint64_t> FirmwareInfoProviderImplementation::get_board_id(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> high = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_HIGH);
    const std::optional<uint32_t> low = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_LOW);
    if (!high.has_value() || !low.has_value()) {
        return std::nullopt;
    }
    return (static_cast<uint64_t>(high.value()) << 32) | low.value();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_eth_fw_version(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_eth_fw_version_semver(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> raw = read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return arch_ == tt::ARCH::BLACKHOLE ? SemVer::from_blackhole_eth_firmware_tag(raw.value())
                                        : SemVer::from_wormhole_eth_firmware_tag(raw.value());
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_gddr_fw_version(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> raw = read_scalar<uint32_t>(FirmwareFeature::GDDR_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_gddr_fw_version_from_telemetry(raw.value(), arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_cm_fw_version(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> raw = read_scalar<uint32_t>(FirmwareFeature::CM_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_cm_fw_version_from_telemetry(raw.value(), arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_app_fw_version(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> raw = read_scalar<uint32_t>(FirmwareFeature::DM_APP_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_app_fw_version_from_telemetry(raw.value(), arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_bl_fw_version(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> raw = read_scalar<uint32_t>(FirmwareFeature::DM_BL_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_bl_fw_version_from_telemetry(raw.value(), arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_tt_flash_version(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> raw = read_scalar<uint32_t>(FirmwareFeature::TT_FLASH_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_tt_flash_version_from_telemetry(raw.value());
}

std::optional<double> FirmwareInfoProviderImplementation::get_asic_temperature(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::ASIC_TEMPERATURE);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_aiclk(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::AICLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_clock_freq(NocId noc_id) const {
    return get_aiclk(noc_id);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_axiclk(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::AXICLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_arcclk(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::ARCCLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_speed(NocId noc_id) const {
    return get_fan_speeds(noc_id).at(0);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_rpm(NocId noc_id) const {
    return get_fan_rpms(noc_id).at(0);
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_speeds(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    // Firmware publishes a single fan; the remaining slots are reported as unavailable.
    std::vector<std::optional<uint32_t>> speeds(MAX_NUMBER_OF_FANS, std::nullopt);
    const std::optional<uint32_t> speed = read_scalar<uint32_t>(FirmwareFeature::FAN_SPEED);
    if (speed != FAN_NOT_CONTROLLED) {
        speeds[0] = speed;
    }
    return speeds;
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_rpms(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    std::vector<std::optional<uint32_t>> rpms(MAX_NUMBER_OF_FANS, std::nullopt);
    const std::optional<uint32_t> rpm = read_scalar<uint32_t>(FirmwareFeature::FAN_RPM);
    if (rpm != FAN_NOT_CONTROLLED) {
        rpms[0] = rpm;
    }
    return rpms;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::TDP);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdc(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::TDC);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_vcore(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::VCORE);
}

std::optional<double> FirmwareInfoProviderImplementation::get_board_temperature(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::BOARD_TEMPERATURE);
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_shutdown(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_SHUTDOWN);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp_limit(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::TDP_LIMIT_MAX);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_board_power_limit(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::BOARD_POWER_LIMIT);
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_throttle(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_THROTTLE);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_therm_trip_count(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::THERM_TRIP_COUNT);
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_heartbeat_status(NocId noc_id) const {
    const auto per_core = get_eth_heartbeat_status_per_core(noc_id);
    if (!per_core.has_value()) {
        return std::nullopt;
    }
    std::vector<bool> statuses;
    for (const auto& [core, status] : per_core.value()) {
        statuses.push_back(status);
    }
    return statuses;
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_retrain_status(NocId noc_id) const {
    const auto per_core = get_eth_retrain_status_per_core(noc_id);
    if (!per_core.has_value()) {
        return std::nullopt;
    }
    std::vector<bool> statuses;
    for (const auto& [core, status] : per_core.value()) {
        statuses.push_back(status);
    }
    return statuses;
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_heartbeat_status_per_core(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint16_t> bitmask = read_scalar<uint16_t>(FirmwareFeature::ETH_HEARTBEAT_STATUS);
    if (!bitmask.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(bitmask.value());
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_retrain_status_per_core(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint16_t> bitmask = read_scalar<uint16_t>(FirmwareFeature::ETH_RETRAIN_STATUS);
    if (!bitmask.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(bitmask.value());
}

std::optional<std::vector<std::pair<CoreCoord, bool>>> FirmwareInfoProviderImplementation::get_eth_link_status_per_core(
    NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint16_t> bitmask = read_scalar<uint16_t>(FirmwareFeature::ETH_LINK_STATUS);
    if (!bitmask.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(bitmask.value());
}

std::vector<DramTrainingStatus> FirmwareInfoProviderImplementation::get_dram_training_status(
    uint32_t num_dram_channels, NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> status_word = read_scalar<uint32_t>(FirmwareFeature::DDR_STATUS);
    if (!status_word.has_value()) {
        return {};
    }

    // Two bits per channel: training complete, then error. A channel that has neither, including a
    // harvested one, is reported as still in progress.
    std::vector<DramTrainingStatus> statuses;
    for (uint32_t channel = 0; channel < num_dram_channels; ++channel) {
        const uint32_t channel_bits = (status_word.value() >> (2 * channel)) & 0b11;
        if (channel_bits & 0b10) {
            statuses.push_back(DramTrainingStatus::FAIL);
        } else if (channel_bits & 0b01) {
            statuses.push_back(DramTrainingStatus::SUCCESS);
        } else {
            statuses.push_back(DramTrainingStatus::IN_PROGRESS);
        }
    }
    return statuses;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_max_clock_freq(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::MAX_CLOCK_FREQ);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_min_clock_freq(NocId noc_id) const {
    // Firmware publishes no minimum; every Blackhole board idles AICLK at the same floor.
    if (arch_ == tt::ARCH::BLACKHOLE) {
        return blackhole::AICLK_IDLE_VAL;
    }
    return std::nullopt;
}

std::optional<uint8_t> FirmwareInfoProviderImplementation::get_asic_location(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint8_t>(FirmwareFeature::ASIC_LOCATION);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_heartbeat(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint32_t>(FirmwareFeature::HEARTBEAT);
}

std::optional<GddrTelemetry> FirmwareInfoProviderImplementation::get_aggregated_dram_telemetry(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    const std::optional<uint32_t> uncorr_word = read_scalar<uint32_t>(FirmwareFeature::GDDR_UNCORR_ERRS);
    if (!uncorr_word.has_value()) {
        return std::nullopt;
    }

    GddrTelemetry telemetry;
    const size_t num_modules = get_number_of_dram_modules(arch_);
    // Modules come in pairs that share their temperature and corrected error words.
    for (size_t index = 0; index < num_modules; index += 2) {
        const GddrModule even_module = static_cast<GddrModule>(index);
        const std::optional<uint32_t> temp_word = read_scalar<uint32_t>(gddr_temp_feature(even_module));
        const std::optional<uint32_t> corr_word = read_scalar<uint32_t>(gddr_corr_errs_feature(even_module));
        if (!temp_word.has_value() || !corr_word.has_value()) {
            return std::nullopt;
        }
        for (size_t module_index = index; module_index < index + 2; ++module_index) {
            const GddrModule module = static_cast<GddrModule>(module_index);
            telemetry.modules[module] =
                decode_gddr_module(module, temp_word.value(), corr_word.value(), uncorr_word.value());
        }
    }
    return telemetry;
}

std::optional<GddrModuleTelemetry> FirmwareInfoProviderImplementation::get_dram_telemetry(
    GddrModule gddr_module, NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    UMD_ASSERT(
        static_cast<size_t>(gddr_module) < get_number_of_dram_modules(arch_),
        error::RuntimeError,
        fmt::format("GDDR module {} does not exist on {}.", static_cast<int>(gddr_module), arch_to_str(arch_)));

    const std::optional<uint32_t> temp_word = read_scalar<uint32_t>(gddr_temp_feature(gddr_module));
    const std::optional<uint32_t> corr_word = read_scalar<uint32_t>(gddr_corr_errs_feature(gddr_module));
    const std::optional<uint32_t> uncorr_word = read_scalar<uint32_t>(FirmwareFeature::GDDR_UNCORR_ERRS);
    if (!temp_word.has_value() || !corr_word.has_value() || !uncorr_word.has_value()) {
        return std::nullopt;
    }
    return decode_gddr_module(gddr_module, temp_word.value(), corr_word.value(), uncorr_word.value());
}

std::optional<uint16_t> FirmwareInfoProviderImplementation::get_dram_speed(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<uint16_t>(FirmwareFeature::DDR_SPEED);
}

std::optional<double> FirmwareInfoProviderImplementation::get_current_max_dram_temperature(NocId noc_id) const {
    NocIdSwitcher noc_switcher(noc_id);
    return read_scalar<double>(FirmwareFeature::MAX_GDDR_TEMP);
}

std::vector<std::pair<CoreCoord, bool>> FirmwareInfoProviderImplementation::parse_eth_status_bitmask(
    uint16_t bitmask) const {
    UMD_ASSERT(
        arch_ == tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        fmt::format("ETH status is not supported on {}.", arch_to_str(arch_)));

    // Bit i is the firmware's ETH instance i, whose NOC0 location is the i-th entry of this list.
    std::vector<std::pair<CoreCoord, bool>> statuses;
    for (size_t instance = 0; instance < blackhole::ETH_CORES_NOC0.size(); ++instance) {
        const tt_xy_pair& core = blackhole::ETH_CORES_NOC0[instance];
        statuses.emplace_back(
            CoreCoord(core.x, core.y, CoreType::ETH, CoordSystem::NOC0), ((bitmask >> instance) & 1) != 0);
    }
    return statuses;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_firmware_feature_map(
    tt::ARCH arch, const FirmwareBundleVersion& fw_version) {
    UMD_ASSERT(
        arch == tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        fmt::format("Firmware information is not supported on {}.", arch_to_str(arch)));

    if (fw_version >= FW_VERSION_19_9) {
        return create_blackhole_19_9_base();
    }
    if (fw_version >= FW_VERSION_19_8) {
        return create_blackhole_19_8_base();
    }
    if (fw_version >= FW_VERSION_18_8) {
        return create_blackhole_18_8_base();
    }
    return create_blackhole_18_5_base();
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_18_4_new_telemetry_base() {
    return {
        {FirmwareFeature::FIRMWARE_VERSION, tag(TelemetryTag::FLASH_BUNDLE_VERSION)},
        {FirmwareFeature::ETH_FW_VERSION, tag(TelemetryTag::ETH_FW_VERSION)},
        {FirmwareFeature::GDDR_FW_VERSION, tag(TelemetryTag::GDDR_FW_VERSION)},
        {FirmwareFeature::CM_FW_VERSION, tag(TelemetryTag::CM_FW_VERSION)},
        {FirmwareFeature::DM_APP_FW_VERSION, tag(TelemetryTag::DM_APP_FW_VERSION)},
        {FirmwareFeature::DM_BL_FW_VERSION, tag(TelemetryTag::DM_BL_FW_VERSION)},
        {FirmwareFeature::TT_FLASH_VERSION, tag(TelemetryTag::TT_FLASH_VERSION)},
        {FirmwareFeature::BOARD_ID_HIGH, tag(TelemetryTag::BOARD_ID_HIGH)},
        {FirmwareFeature::BOARD_ID_LOW, tag(TelemetryTag::BOARD_ID_LOW)},
        {FirmwareFeature::ASIC_LOCATION, tag(TelemetryTag::ASIC_LOCATION)},
        {FirmwareFeature::ASIC_TEMPERATURE, tag(TelemetryTag::ASIC_TEMPERATURE, S16_16)},
        {FirmwareFeature::BOARD_TEMPERATURE, tag(TelemetryTag::BOARD_TEMPERATURE, S16_16)},
        {FirmwareFeature::GDDR_0_1_TEMP, tag(TelemetryTag::GDDR_0_1_TEMP)},
        {FirmwareFeature::GDDR_2_3_TEMP, tag(TelemetryTag::GDDR_2_3_TEMP)},
        {FirmwareFeature::GDDR_4_5_TEMP, tag(TelemetryTag::GDDR_4_5_TEMP)},
        {FirmwareFeature::GDDR_6_7_TEMP, tag(TelemetryTag::GDDR_6_7_TEMP)},
        {FirmwareFeature::MAX_GDDR_TEMP, tag(TelemetryTag::MAX_GDDR_TEMP)},
        {FirmwareFeature::AICLK, tag(TelemetryTag::AICLK)},
        {FirmwareFeature::AXICLK, tag(TelemetryTag::AXICLK)},
        {FirmwareFeature::ARCCLK, tag(TelemetryTag::ARCCLK)},
        {FirmwareFeature::MAX_CLOCK_FREQ, tag(TelemetryTag::AICLK_LIMIT_MAX)},
        {FirmwareFeature::DDR_SPEED, tag(TelemetryTag::GDDR_SPEED)},
        {FirmwareFeature::TDP, tag(TelemetryTag::TDP)},
        {FirmwareFeature::TDC, tag(TelemetryTag::TDC)},
        {FirmwareFeature::VCORE, tag(TelemetryTag::VCORE)},
        {FirmwareFeature::TDC_LIMIT_MAX, tag(TelemetryTag::TDC_LIMIT_MAX)},
        {FirmwareFeature::TDP_LIMIT_MAX, tag(TelemetryTag::TDP_LIMIT_MAX)},
        {FirmwareFeature::BOARD_POWER_LIMIT, tag(TelemetryTag::BOARD_POWER_LIMIT)},
        {FirmwareFeature::FAN_SPEED, tag(TelemetryTag::FAN_SPEED)},
        {FirmwareFeature::FAN_RPM, tag(TelemetryTag::FAN_RPM)},
        {FirmwareFeature::THM_LIMIT_THROTTLE, tag(TelemetryTag::THM_LIMIT_THROTTLE)},
        {FirmwareFeature::THM_LIMIT_SHUTDOWN, tag(TelemetryTag::THM_LIMIT_SHUTDOWN)},
        {FirmwareFeature::DDR_STATUS, tag(TelemetryTag::GDDR_STATUS)},
        {FirmwareFeature::HEARTBEAT, tag(TelemetryTag::TIMER_HEARTBEAT)},
        // ETH_LIVE_STATUS: heartbeat bits in [15:0], link bits in [31:16].
        {FirmwareFeature::ETH_HEARTBEAT_STATUS, tag(TelemetryTag::ETH_LIVE_STATUS, {0, 0xFFFF})},
        {FirmwareFeature::ETH_RETRAIN_STATUS, not_available()},
        {FirmwareFeature::ETH_LINK_STATUS, tag(TelemetryTag::ETH_LIVE_STATUS, {16, 0xFFFF})},
        {FirmwareFeature::THERM_TRIP_COUNT, tag(TelemetryTag::THERM_TRIP_COUNT)},
        {FirmwareFeature::GDDR_UNCORR_ERRS, tag(TelemetryTag::GDDR_UNCORR_ERRS)},
        {FirmwareFeature::GDDR_0_1_CORR_ERRS, tag(TelemetryTag::GDDR_0_1_CORR_ERRS)},
        {FirmwareFeature::GDDR_2_3_CORR_ERRS, tag(TelemetryTag::GDDR_2_3_CORR_ERRS)},
        {FirmwareFeature::GDDR_4_5_CORR_ERRS, tag(TelemetryTag::GDDR_4_5_CORR_ERRS)},
        {FirmwareFeature::GDDR_6_7_CORR_ERRS, tag(TelemetryTag::GDDR_6_7_CORR_ERRS)},
    };
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_5_base() {
    FirmwareFeatures features = create_18_4_new_telemetry_base();
    // The ETH firmware version is read from the ETH cores directly, never from telemetry, and the
    // TT-Flash version is never published on Blackhole.
    features[FirmwareFeature::ETH_FW_VERSION] = not_available();
    features[FirmwareFeature::TT_FLASH_VERSION] = not_available();
    // Before 18.8 the firmware publishes no clock or thermal limits; the clock limit is the busy AICLK.
    features[FirmwareFeature::MAX_CLOCK_FREQ] =
        FeatureProfile{FixedValue{blackhole::AICLK_BUSY_VAL}, LinearTransform{}};
    features[FirmwareFeature::THM_LIMIT_SHUTDOWN] = not_available();
    features[FirmwareFeature::THM_LIMIT_THROTTLE] = not_available();
    features[FirmwareFeature::TDC_LIMIT_MAX] = not_available();
    // The TDP limit tracks SET_TDP_LIMIT only from 19.8.
    features[FirmwareFeature::TDP_LIMIT_MAX] = not_available();
    // ETH_LIVE_STATUS stays 0 before 19.9.
    features[FirmwareFeature::ETH_HEARTBEAT_STATUS] = not_available();
    features[FirmwareFeature::ETH_LINK_STATUS] = not_available();
    return features;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_8_base() {
    FirmwareFeatures features = create_blackhole_18_5_base();
    features[FirmwareFeature::MAX_CLOCK_FREQ] = tag(TelemetryTag::AICLK_LIMIT_MAX);
    features[FirmwareFeature::THM_LIMIT_SHUTDOWN] = tag(TelemetryTag::THM_LIMIT_SHUTDOWN);
    features[FirmwareFeature::THM_LIMIT_THROTTLE] = tag(TelemetryTag::THM_LIMIT_THROTTLE);
    features[FirmwareFeature::TDC_LIMIT_MAX] = tag(TelemetryTag::TDC_LIMIT_MAX);
    return features;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_8_base() {
    FirmwareFeatures features = create_blackhole_18_8_base();
    features[FirmwareFeature::TDP_LIMIT_MAX] = tag(TelemetryTag::TDP_LIMIT_MAX);
    return features;
}

FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_9_base() {
    FirmwareFeatures features = create_blackhole_19_8_base();
    features[FirmwareFeature::ETH_HEARTBEAT_STATUS] = tag(TelemetryTag::ETH_LIVE_STATUS, {0, 0xFFFF});
    features[FirmwareFeature::ETH_LINK_STATUS] = tag(TelemetryTag::ETH_LIVE_STATUS, {16, 0xFFFF});
    return features;
}

uint32_t FirmwareInfoProviderImplementation::read_raw_telemetry(const FeatureKey& key) const {
    return std::visit(
        [this](const auto& source) -> uint32_t {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, StandardTag>) {
                return telemetry_->read_entry(source, get_selected_noc_id());
            } else if constexpr (std::is_same_v<Source, FixedValue>) {
                return source.value;
            } else {
                UMD_THROW(error::RuntimeError, "Legacy Wormhole telemetry is not supported.");
            }
        },
        key);
}

bool FirmwareInfoProviderImplementation::is_feature_available(FirmwareFeature feature) const {
    const auto profile = firmware_feature_map.find(feature);
    if (profile == firmware_feature_map.end() || std::holds_alternative<NotAvailable>(profile->second.converter)) {
        return false;
    }
    // Tags may be missing from the firmware's table even where the feature map expects them.
    if (const StandardTag* tag = std::get_if<StandardTag>(&profile->second.key)) {
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
    const LinearTransform& transform = std::get<LinearTransform>(profile.converter);

    const uint32_t masked = (read_raw_telemetry(profile.key) >> transform.shift) & transform.mask;
    const double value = transform.signedness == NumericSign::SIGNED ? static_cast<double>(static_cast<int32_t>(masked))
                                                                     : static_cast<double>(masked);
    return static_cast<T>(value * transform.scale + transform.offset);
}

}  // namespace tt::umd
