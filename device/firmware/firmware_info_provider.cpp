// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/firmware_info_provider.hpp"

#include <cstdint>

#include "assert.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_dram.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

FirmwareInfoProvider::FirmwareInfoProvider(TTDevice* tt_device) :
    tt_device(tt_device), firmware_version(get_firmware_version_util(tt_device)) {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (telemetry == nullptr) {
        TT_THROW("No telemetry reader present in tt_device.");
    }

    // Build the telemetry feature map based on architecture and version.
    telemetry_feature_map = create_telemetry_feature_map(tt_device, firmware_version);
}

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProvider::create_firmware_info_provider(TTDevice* tt_device) {
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
        case ARCH::BLACKHOLE:
            return std::make_unique<FirmwareInfoProvider>(tt_device);
        default:
            TT_THROW("Unsupported architecture for firmware versioner.");
    }
}

TelemetryFeatureMap FirmwareInfoProvider::create_telemetry_feature_map(
    TTDevice* tt_device, const semver_t& fw_version) {
    static const semver_t fw_version_18_7 = semver_t(18, 7, 0);
    static const semver_t fw_version_18_3 = semver_t(18, 3, 0);

    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            if (semver_t::compare_firmware_bundle(fw_version, fw_version_18_3) <= 0) {
                // Legacy Wormhole <= 18.3.
                return create_legacy_wormhole_18_3_base();
            } else if (semver_t::compare_firmware_bundle(fw_version, fw_version_18_7) <= 0) {
                // Legacy Wormhole 18.4 - 18.7.
                TelemetryFeatureMap map = create_modern_base();
                map[FirmwareFeature::MAX_CLOCK_FREQ] = {
                    SmBusTag{wormhole::TelemetryTag::AICLK}, LinearTransform{16, 0xFFFF, 1.0, 0.0}};
                return map;
            }
            // Modern Wormhole > 18.7.
            return create_modern_base();
        case ARCH::BLACKHOLE:
            if (semver_t::compare_firmware_bundle(fw_version, fw_version_18_7) <= 0) {
                // Legacy Blackhole <= 18.7.
                TelemetryFeatureMap map = create_modern_base();
                map[FirmwareFeature::MAX_CLOCK_FREQ] = {FixedValue{blackhole::AICLK_BUSY_VAL}, LinearTransform{}};
                return map;
            }
            // Modern Blackhole > 18.7.
            return create_modern_base();
        default:
            TT_THROW("Unsupported architecture for telemetry feature map.");
    }
}

// Create base map for modern firmware (StandardTag).
TelemetryFeatureMap FirmwareInfoProvider::create_modern_base() {
    return {
        {FirmwareFeature::BOARD_ID_HIGH, {TelemetryTag::BOARD_ID_HIGH, LinearTransform{}}},
        {FirmwareFeature::BOARD_ID_LOW, {TelemetryTag::BOARD_ID_LOW, LinearTransform{}}},
        {FirmwareFeature::ASIC_TEMPERATURE,
         {TelemetryTag::ASIC_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::BOARD_TEMPERATURE,
         {TelemetryTag::BOARD_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::AICLK, {TelemetryTag::AICLK, LinearTransform{}}},
        {FirmwareFeature::AXICLK, {TelemetryTag::AXICLK, LinearTransform{}}},
        {FirmwareFeature::ARCCLK, {TelemetryTag::ARCCLK, LinearTransform{}}},
        {FirmwareFeature::MAX_CLOCK_FREQ, {TelemetryTag::AICLK_LIMIT_MAX, LinearTransform{}}},
        {FirmwareFeature::FAN_SPEED, {TelemetryTag::FAN_SPEED, LinearTransform{}}},
        {FirmwareFeature::TDP, {TelemetryTag::TDP, LinearTransform{}}},
        {FirmwareFeature::TDC, {TelemetryTag::TDC, LinearTransform{}}},
        {FirmwareFeature::VCORE, {TelemetryTag::VCORE, LinearTransform{}}},
        {FirmwareFeature::DDR_STATUS, {TelemetryTag::DDR_STATUS, LinearTransform{}}},
        {FirmwareFeature::ASIC_LOCATION, {TelemetryTag::ASIC_LOCATION, LinearTransform{}}},
        {FirmwareFeature::HEARTBEAT, {TelemetryTag::TIMER_HEARTBEAT, LinearTransform{}}},
        {FirmwareFeature::ETH_FW_VERSION, {TelemetryTag::ETH_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::GDDR_FW_VERSION, {TelemetryTag::GDDR_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::CM_FW_VERSION, {TelemetryTag::CM_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::DM_APP_FW_VERSION, {TelemetryTag::DM_APP_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::DM_BL_FW_VERSION, {TelemetryTag::DM_BL_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::TT_FLASH_VERSION, {TelemetryTag::TT_FLASH_VERSION, LinearTransform{}}},
    };
}

// Create base map for legacy Wormhole 18.3 firmware (WormholeTag).
TelemetryFeatureMap FirmwareInfoProvider::create_legacy_wormhole_18_3_base() {
    return {
        {FirmwareFeature::BOARD_ID_HIGH, {wormhole::TelemetryTag::BOARD_ID_HIGH, LinearTransform{}}},
        {FirmwareFeature::BOARD_ID_LOW, {wormhole::TelemetryTag::BOARD_ID_LOW, LinearTransform{}}},
        {FirmwareFeature::ASIC_TEMPERATURE,
         {wormhole::TelemetryTag::ASIC_TEMPERATURE, LinearTransform{0, 0xFFFF, 1.0 / 16.0, 0.0}}},
        {FirmwareFeature::BOARD_TEMPERATURE,
         {wormhole::TelemetryTag::BOARD_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::AICLK, {wormhole::TelemetryTag::AICLK, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::AXICLK, {wormhole::TelemetryTag::AXICLK, LinearTransform{}}},
        {FirmwareFeature::ARCCLK, {wormhole::TelemetryTag::ARCCLK, LinearTransform{}}},
        {FirmwareFeature::MAX_CLOCK_FREQ,
         {SmBusTag{wormhole::TelemetryTag::AICLK}, LinearTransform{16, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::FAN_SPEED, {wormhole::TelemetryTag::FAN_SPEED, LinearTransform{}}},
        {FirmwareFeature::TDP, {wormhole::TelemetryTag::TDP, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::TDC, {wormhole::TelemetryTag::TDC, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::VCORE, {wormhole::TelemetryTag::VCORE, LinearTransform{}}},
        {FirmwareFeature::DDR_STATUS, {wormhole::TelemetryTag::DDR_STATUS, LinearTransform{}}},
        {FirmwareFeature::ASIC_LOCATION, {FixedValue{0}, LinearTransform{}}},
        {FirmwareFeature::HEARTBEAT, {wormhole::TelemetryTag::ARC0_HEALTH, LinearTransform{}}},
        {FirmwareFeature::ETH_FW_VERSION, {wormhole::TelemetryTag::ETH_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::GDDR_FW_VERSION, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::CM_FW_VERSION, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::DM_APP_FW_VERSION, {wormhole::TelemetryTag::DM_APP_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::DM_BL_FW_VERSION, {wormhole::TelemetryTag::DM_BL_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::TT_FLASH_VERSION, {wormhole::TelemetryTag::TT_FLASH_VERSION, LinearTransform{}}},
    };
}

uint32_t FirmwareInfoProvider::read_raw_telemetry(const TelemetryKey& key) const {
    return std::visit(
        [this](auto&& arg) -> uint32_t {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, StandardTag>) {
                auto* tel = tt_device->get_arc_telemetry_reader();
                return (tel && tel->is_entry_available(arg)) ? tel->read_entry(arg) : 0;
            } else if constexpr (std::is_same_v<T, WormholeTag>) {
                auto* tel = tt_device->get_arc_telemetry_reader();
                return (tel && tel->is_entry_available(arg)) ? tel->read_entry(arg) : 0;
            } else if constexpr (std::is_same_v<T, SmBusTag>) {
                const std::unique_ptr<SmBusArcTelemetryReader> sm_bus_telemetry =
                    std::make_unique<SmBusArcTelemetryReader>(tt_device);
                return sm_bus_telemetry->read_entry(arg.tag);
            } else if constexpr (std::is_same_v<T, FixedValue>) {
                return arg.value;
            }
            return 0u;
        },
        key);
}

bool FirmwareInfoProvider::is_feature_available(FirmwareFeature feature) const {
    auto it = telemetry_feature_map.find(feature);
    if (it == telemetry_feature_map.end()) {
        return false;
    }

    // Check if converter indicates not available.
    if (std::holds_alternative<NotAvailable>(it->second.converter)) {
        return false;
    }

    // Check if key is available in telemetry.
    return std::visit(
        [this](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, StandardTag>) {
                auto* tel = tt_device->get_arc_telemetry_reader();
                return tel && tel->is_entry_available(arg);
            } else if constexpr (std::is_same_v<T, WormholeTag>) {
                auto* tel = tt_device->get_arc_telemetry_reader();
                return tel && tel->is_entry_available(arg);
            } else if constexpr (std::is_same_v<T, SmBusTag>) {
                return true;  // SMBus is always available
            } else if constexpr (std::is_same_v<T, FixedValue>) {
                return true;  // Fixed values are always available
            }
            return false;
        },
        it->second.key);
}

template <typename T>
std::optional<T> FirmwareInfoProvider::read_scalar(FirmwareFeature feature) const {
    auto it = telemetry_feature_map.find(feature);
    if (it == telemetry_feature_map.end()) {
        return std::nullopt;
    }

    // Check if feature is not available.
    if (std::holds_alternative<NotAvailable>(it->second.converter)) {
        return std::nullopt;
    }

    uint32_t raw = read_raw_telemetry(it->second.key);

    // Apply the converter.
    return std::visit(
        [raw](auto&& converter) -> std::optional<T> {
            using C = std::decay_t<decltype(converter)>;

            if constexpr (std::is_same_v<C, LinearTransform>) {
                double result =
                    static_cast<double>((raw >> converter.shift) & converter.mask) * converter.scale + converter.offset;
                return static_cast<T>(result);
            } else if constexpr (std::is_same_v<C, NotAvailable>) {
                return std::nullopt;
            }
            return std::nullopt;
        },
        it->second.converter);
}

// Explicit instantiations.
template std::optional<uint32_t> FirmwareInfoProvider::read_scalar<uint32_t>(FirmwareFeature feature) const;
template std::optional<double> FirmwareInfoProvider::read_scalar<double>(FirmwareFeature feature) const;
template std::optional<uint8_t> FirmwareInfoProvider::read_scalar<uint8_t>(FirmwareFeature feature) const;

semver_t FirmwareInfoProvider::get_firmware_version() const { return firmware_version; }

semver_t FirmwareInfoProvider::get_latest_supported_firmware_version(tt::ARCH arch) { return semver_t(19, 4, 0); }

semver_t FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            return semver_t(0, 0, 0);
        }
        case tt::ARCH::BLACKHOLE: {
            return semver_t(18, 5, 0);
        }
        default:
            TT_THROW("Unsupported architecture for firmware info provider.");
    }
}

uint64_t FirmwareInfoProvider::get_board_id() const {
    uint32_t high = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_HIGH).value_or(0);
    uint32_t low = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_LOW).value_or(0);
    return (static_cast<uint64_t>(high) << 32) | low;
}

uint32_t FirmwareInfoProvider::get_eth_fw_version() const {
    return read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION).value_or(0);
}

std::optional<semver_t> FirmwareInfoProvider::get_eth_fw_version_semver() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_eth_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_gddr_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::GDDR_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_gddr_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_cm_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::CM_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_cm_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_dm_app_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::DM_APP_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_app_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_dm_bl_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::DM_BL_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_bl_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_tt_flash_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::TT_FLASH_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_tt_flash_version_from_telemetry(*raw);
}

double FirmwareInfoProvider::get_asic_temperature() const {
    return read_scalar<double>(FirmwareFeature::ASIC_TEMPERATURE).value_or(0.0);
}

std::optional<double> FirmwareInfoProvider::get_board_temperature() const {
    return read_scalar<double>(FirmwareFeature::BOARD_TEMPERATURE);
}

uint32_t FirmwareInfoProvider::get_max_clock_freq() const {
    return read_scalar<uint32_t>(FirmwareFeature::MAX_CLOCK_FREQ).value_or(0);
}

std::optional<uint32_t> FirmwareInfoProvider::get_aiclk() const {
    return read_scalar<uint32_t>(FirmwareFeature::AICLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_axiclk() const {
    return read_scalar<uint32_t>(FirmwareFeature::AXICLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_arcclk() const {
    return read_scalar<uint32_t>(FirmwareFeature::ARCCLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_fan_speed() const {
    auto fan_speed = read_scalar<uint32_t>(FirmwareFeature::FAN_SPEED);
    // All ones mean fans not present on board, or not under control of firmware.
    if (fan_speed.has_value() && fan_speed.value() == 0xFFFFFFFF) {
        return std::nullopt;
    }
    return fan_speed;
}

std::optional<uint32_t> FirmwareInfoProvider::get_tdp() const { return read_scalar<uint32_t>(FirmwareFeature::TDP); }

std::optional<uint32_t> FirmwareInfoProvider::get_tdc() const { return read_scalar<uint32_t>(FirmwareFeature::TDC); }

std::optional<uint32_t> FirmwareInfoProvider::get_vcore() const {
    return read_scalar<uint32_t>(FirmwareFeature::VCORE);
}

uint8_t FirmwareInfoProvider::get_asic_location() const {
    return read_scalar<uint8_t>(FirmwareFeature::ASIC_LOCATION).value_or(0);
}

uint32_t FirmwareInfoProvider::get_heartbeat() const {
    return read_scalar<uint32_t>(FirmwareFeature::HEARTBEAT).value_or(0);
}

// Legacy Wormhole: Each channel uses 4 bits.
static std::vector<DramTrainingStatus> get_legacy_wormhole_dram_statuses(
    uint32_t telemetry_data, uint32_t num_channels) {
    std::vector<DramTrainingStatus> statuses;
    for (uint32_t channel = 0; channel < num_channels; ++channel) {
        uint8_t status = (telemetry_data >> (channel * 4)) & 0xF;
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

// Modern format: Each channel gets two bits in the 32-bit value (16 bits used).
// The lower bits are for lower channels.
// Lower of the two bits reports the training status and higher of the two bits reports the training error.
// Example: 0b 00 00 00 00 00 00 01 10
// would mean that only channel 0 is trained, channel 1 has the error and other channels are not trained
// and don't have errors. If some channel is harvested the bits are always going to be zero.
static std::vector<DramTrainingStatus> get_modern_dram_statuses(uint32_t telemetry_data, uint32_t num_channels) {
    std::vector<DramTrainingStatus> statuses;
    for (uint32_t channel = 0; channel < num_channels; ++channel) {
        uint8_t status = (telemetry_data >> (channel * 2)) & 0x3;
        switch (status) {
            case 0b01:
                statuses.push_back(DramTrainingStatus::SUCCESS);
                break;
            case 0b10:
            case 0b11:
                statuses.push_back(DramTrainingStatus::FAIL);
                break;
            default:  // 0b00
                statuses.push_back(DramTrainingStatus::IN_PROGRESS);
                break;
        }
    }
    return statuses;
}

std::vector<DramTrainingStatus> FirmwareInfoProvider::get_dram_training_status(uint32_t num_dram_channels) const {
    uint32_t telemetry_data = read_scalar<uint32_t>(FirmwareFeature::DDR_STATUS).value_or(0);

    // Check if we're using legacy Wormhole format (4 bits per channel)
    // or modern format (2 bits per channel).
    static const semver_t fw_version_18_3 = semver_t(18, 3, 0);
    bool is_legacy_wormhole = tt_device->get_arch() == ARCH::WORMHOLE_B0 &&
                              semver_t::compare_firmware_bundle(firmware_version, fw_version_18_3) <= 0;

    return is_legacy_wormhole ? get_legacy_wormhole_dram_statuses(telemetry_data, num_dram_channels)
                              : get_modern_dram_statuses(telemetry_data, num_dram_channels);
}

}  // namespace tt::umd
