// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/firmware_info_provider.hpp"

#include <cstdint>
#include <stdexcept>

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
        throw std::runtime_error("No telemetry reader present in tt_device.");
    }

    // Build the telemetry feature map based on architecture and version.
    telemetry_feature_map = create_telemetry_feature_map(tt_device, firmware_version);

    // Cache availability flags for optional features.
    aiclk_available = is_feature_available(TelemetryFeature::AICLK);
    axiclk_available = is_feature_available(TelemetryFeature::AXICLK);
    arcclk_available = is_feature_available(TelemetryFeature::ARCCLK);
    fan_speed_available = is_feature_available(TelemetryFeature::FAN_SPEED);
    tdp_available = is_feature_available(TelemetryFeature::TDP);
    tdc_available = is_feature_available(TelemetryFeature::TDC);
    vcore_available = is_feature_available(TelemetryFeature::VCORE);
    board_temperature_available = is_feature_available(TelemetryFeature::BOARD_TEMPERATURE);
}

std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProvider::create_firmware_info_provider(TTDevice* tt_device) {
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
        case ARCH::BLACKHOLE:
            return std::make_unique<FirmwareInfoProvider>(tt_device);
        default:
            throw std::runtime_error("Unsupported architecture for firmware versioner.");
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
                map[TelemetryFeature::MAX_CLOCK_FREQ] = {
                    SmBusTag{wormhole::TelemetryTag::AICLK}, LinearTransform{16, 0xFFFF, 1.0, 0.0}};
                return map;
            }
            // Modern Wormhole > 18.7.
            return create_modern_base();
        case ARCH::BLACKHOLE:
            if (semver_t::compare_firmware_bundle(fw_version, fw_version_18_7) <= 0) {
                // Legacy Blackhole <= 18.7.
                TelemetryFeatureMap map = create_modern_base();
                map[TelemetryFeature::MAX_CLOCK_FREQ] = {FixedValue{blackhole::AICLK_BUSY_VAL}, LinearTransform{}};
                return map;
            }
            // Modern Blackhole > 18.7.
            return create_modern_base();
        default:
            return create_modern_base();
    }
}

// Create base map for modern firmware (StandardTag).
TelemetryFeatureMap FirmwareInfoProvider::create_modern_base() {
    return {
        {TelemetryFeature::BOARD_ID_HIGH, {TelemetryTag::BOARD_ID_HIGH, LinearTransform{}}},
        {TelemetryFeature::BOARD_ID_LOW, {TelemetryTag::BOARD_ID_LOW, LinearTransform{}}},
        {TelemetryFeature::ASIC_TEMPERATURE,
         {TelemetryTag::ASIC_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {TelemetryFeature::BOARD_TEMPERATURE,
         {TelemetryTag::BOARD_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {TelemetryFeature::AICLK, {TelemetryTag::AICLK, LinearTransform{}}},
        {TelemetryFeature::AXICLK, {TelemetryTag::AXICLK, LinearTransform{}}},
        {TelemetryFeature::ARCCLK, {TelemetryTag::ARCCLK, LinearTransform{}}},
        {TelemetryFeature::MAX_CLOCK_FREQ, {TelemetryTag::AICLK_LIMIT_MAX, LinearTransform{}}},
        {TelemetryFeature::FAN_SPEED, {TelemetryTag::FAN_SPEED, LinearTransform{}}},
        {TelemetryFeature::TDP, {TelemetryTag::TDP, LinearTransform{}}},
        {TelemetryFeature::TDC, {TelemetryTag::TDC, LinearTransform{}}},
        {TelemetryFeature::VCORE, {TelemetryTag::VCORE, LinearTransform{}}},
        {TelemetryFeature::DDR_STATUS, {TelemetryTag::DDR_STATUS, LinearTransform{}}},
        {TelemetryFeature::ASIC_LOCATION, {TelemetryTag::ASIC_LOCATION, LinearTransform{}}},
        {TelemetryFeature::HEARTBEAT, {TelemetryTag::TIMER_HEARTBEAT, LinearTransform{}}},
        {TelemetryFeature::ETH_FW_VERSION, {TelemetryTag::ETH_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::GDDR_FW_VERSION, {TelemetryTag::GDDR_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::CM_FW_VERSION, {TelemetryTag::CM_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::DM_APP_FW_VERSION, {TelemetryTag::DM_APP_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::DM_BL_FW_VERSION, {TelemetryTag::DM_BL_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::TT_FLASH_VERSION, {TelemetryTag::TT_FLASH_VERSION, LinearTransform{}}},
    };
}

// Create base map for legacy Wormhole 18.3 firmware (WormholeTag).
TelemetryFeatureMap FirmwareInfoProvider::create_legacy_wormhole_18_3_base() {
    return {
        {TelemetryFeature::BOARD_ID_HIGH, {wormhole::TelemetryTag::BOARD_ID_HIGH, LinearTransform{}}},
        {TelemetryFeature::BOARD_ID_LOW, {wormhole::TelemetryTag::BOARD_ID_LOW, LinearTransform{}}},
        {TelemetryFeature::ASIC_TEMPERATURE,
         {wormhole::TelemetryTag::ASIC_TEMPERATURE, LinearTransform{0, 0xFFFF, 1.0 / 16.0, 0.0}}},
        {TelemetryFeature::BOARD_TEMPERATURE,
         {wormhole::TelemetryTag::BOARD_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {TelemetryFeature::AICLK, {wormhole::TelemetryTag::AICLK, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {TelemetryFeature::AXICLK, {wormhole::TelemetryTag::AXICLK, LinearTransform{}}},
        {TelemetryFeature::ARCCLK, {wormhole::TelemetryTag::ARCCLK, LinearTransform{}}},
        {TelemetryFeature::MAX_CLOCK_FREQ,
         {SmBusTag{wormhole::TelemetryTag::AICLK}, LinearTransform{16, 0xFFFF, 1.0, 0.0}}},
        {TelemetryFeature::FAN_SPEED, {wormhole::TelemetryTag::FAN_SPEED, LinearTransform{}}},
        {TelemetryFeature::TDP, {wormhole::TelemetryTag::TDP, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {TelemetryFeature::TDC, {wormhole::TelemetryTag::TDC, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {TelemetryFeature::VCORE, {wormhole::TelemetryTag::VCORE, LinearTransform{}}},
        {TelemetryFeature::DDR_STATUS, {wormhole::TelemetryTag::DDR_STATUS, LinearTransform{}}},
        {TelemetryFeature::ASIC_LOCATION, {FixedValue{0}, LinearTransform{}}},
        {TelemetryFeature::HEARTBEAT, {wormhole::TelemetryTag::ARC0_HEALTH, LinearTransform{}}},
        {TelemetryFeature::ETH_FW_VERSION, {wormhole::TelemetryTag::ETH_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::GDDR_FW_VERSION, {FixedValue{0}, NotAvailable{}}},
        {TelemetryFeature::CM_FW_VERSION, {FixedValue{0}, NotAvailable{}}},
        {TelemetryFeature::DM_APP_FW_VERSION, {wormhole::TelemetryTag::DM_APP_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::DM_BL_FW_VERSION, {wormhole::TelemetryTag::DM_BL_FW_VERSION, LinearTransform{}}},
        {TelemetryFeature::TT_FLASH_VERSION, {wormhole::TelemetryTag::TT_FLASH_VERSION, LinearTransform{}}},
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

bool FirmwareInfoProvider::is_feature_available(TelemetryFeature feature) const {
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
std::optional<T> FirmwareInfoProvider::read_scalar(TelemetryFeature feature) const {
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
template std::optional<uint32_t> FirmwareInfoProvider::read_scalar<uint32_t>(TelemetryFeature feature) const;
template std::optional<double> FirmwareInfoProvider::read_scalar<double>(TelemetryFeature feature) const;
template std::optional<uint8_t> FirmwareInfoProvider::read_scalar<uint8_t>(TelemetryFeature feature) const;

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
            throw std::runtime_error("Unsupported architecture for firmware info provider.");
    }
}

uint64_t FirmwareInfoProvider::get_board_id() const {
    uint32_t high = read_scalar<uint32_t>(TelemetryFeature::BOARD_ID_HIGH).value_or(0);
    uint32_t low = read_scalar<uint32_t>(TelemetryFeature::BOARD_ID_LOW).value_or(0);
    return (static_cast<uint64_t>(high) << 32) | low;
}

uint32_t FirmwareInfoProvider::get_eth_fw_version() const {
    return read_scalar<uint32_t>(TelemetryFeature::ETH_FW_VERSION).value_or(0);
}

std::optional<semver_t> FirmwareInfoProvider::get_eth_fw_version_semver() const {
    if (!is_feature_available(TelemetryFeature::ETH_FW_VERSION)) {
        return std::nullopt;
    }
    uint32_t raw = read_scalar<uint32_t>(TelemetryFeature::ETH_FW_VERSION).value_or(0);
    return get_eth_fw_version_from_telemetry(raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_gddr_fw_version() const {
    if (!is_feature_available(TelemetryFeature::GDDR_FW_VERSION)) {
        return std::nullopt;
    }
    uint32_t raw = read_scalar<uint32_t>(TelemetryFeature::GDDR_FW_VERSION).value_or(0);
    return get_gddr_fw_version_from_telemetry(raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_cm_fw_version() const {
    if (!is_feature_available(TelemetryFeature::CM_FW_VERSION)) {
        return std::nullopt;
    }
    uint32_t raw = read_scalar<uint32_t>(TelemetryFeature::CM_FW_VERSION).value_or(0);
    return get_cm_fw_version_from_telemetry(raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_dm_app_fw_version() const {
    if (!is_feature_available(TelemetryFeature::DM_APP_FW_VERSION)) {
        return std::nullopt;
    }
    uint32_t raw = read_scalar<uint32_t>(TelemetryFeature::DM_APP_FW_VERSION).value_or(0);
    return get_dm_app_fw_version_from_telemetry(raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_dm_bl_fw_version() const {
    if (!is_feature_available(TelemetryFeature::DM_BL_FW_VERSION)) {
        return std::nullopt;
    }
    uint32_t raw = read_scalar<uint32_t>(TelemetryFeature::DM_BL_FW_VERSION).value_or(0);
    return get_dm_bl_fw_version_from_telemetry(raw, tt_device->get_arch());
}

std::optional<semver_t> FirmwareInfoProvider::get_tt_flash_version() const {
    if (!is_feature_available(TelemetryFeature::TT_FLASH_VERSION)) {
        return std::nullopt;
    }
    uint32_t raw = read_scalar<uint32_t>(TelemetryFeature::TT_FLASH_VERSION).value_or(0);
    return get_tt_flash_version_from_telemetry(raw);
}

double FirmwareInfoProvider::get_asic_temperature() const {
    return read_scalar<double>(TelemetryFeature::ASIC_TEMPERATURE).value_or(0.0);
}

std::optional<double> FirmwareInfoProvider::get_board_temperature() const {
    if (!board_temperature_available) {
        return std::nullopt;
    }
    return read_scalar<double>(TelemetryFeature::BOARD_TEMPERATURE);
}

uint32_t FirmwareInfoProvider::get_max_clock_freq() const {
    return read_scalar<uint32_t>(TelemetryFeature::MAX_CLOCK_FREQ).value_or(0);
}

std::optional<uint32_t> FirmwareInfoProvider::get_aiclk() const {
    if (!aiclk_available) {
        return std::nullopt;
    }
    return read_scalar<uint32_t>(TelemetryFeature::AICLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_axiclk() const {
    if (!axiclk_available) {
        return std::nullopt;
    }
    return read_scalar<uint32_t>(TelemetryFeature::AXICLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_arcclk() const {
    if (!arcclk_available) {
        return std::nullopt;
    }
    return read_scalar<uint32_t>(TelemetryFeature::ARCCLK);
}

std::optional<uint32_t> FirmwareInfoProvider::get_fan_speed() const {
    if (!fan_speed_available) {
        return std::nullopt;
    }
    auto fan_speed = read_scalar<uint32_t>(TelemetryFeature::FAN_SPEED);
    // All ones mean fans not present on board, or not under control of firmware.
    if (fan_speed.has_value() && fan_speed.value() == 0xFFFFFFFF) {
        return std::nullopt;
    }
    return fan_speed;
}

std::optional<uint32_t> FirmwareInfoProvider::get_tdp() const {
    if (!tdp_available) {
        return std::nullopt;
    }
    return read_scalar<uint32_t>(TelemetryFeature::TDP);
}

std::optional<uint32_t> FirmwareInfoProvider::get_tdc() const {
    if (!tdc_available) {
        return std::nullopt;
    }
    return read_scalar<uint32_t>(TelemetryFeature::TDC);
}

std::optional<uint32_t> FirmwareInfoProvider::get_vcore() const {
    if (!vcore_available) {
        return std::nullopt;
    }
    return read_scalar<uint32_t>(TelemetryFeature::VCORE);
}

uint8_t FirmwareInfoProvider::get_asic_location() const {
    return read_scalar<uint8_t>(TelemetryFeature::ASIC_LOCATION).value_or(0);
}

uint32_t FirmwareInfoProvider::get_heartbeat() const {
    return read_scalar<uint32_t>(TelemetryFeature::HEARTBEAT).value_or(0);
}

std::vector<DramTrainingStatus> FirmwareInfoProvider::get_dram_training_status(uint32_t num_dram_channels) const {
    uint32_t telemetry_data = read_scalar<uint32_t>(TelemetryFeature::DDR_STATUS).value_or(0);
    std::vector<DramTrainingStatus> statuses;

    // Check if we're using legacy Wormhole format (4 bits per channel)
    // or modern format (2 bits per channel)
    static const semver_t fw_version_18_3 = semver_t(18, 3, 0);
    bool is_legacy_wormhole = tt_device->get_arch() == ARCH::WORMHOLE_B0 &&
                              semver_t::compare_firmware_bundle(firmware_version, fw_version_18_3) <= 0;

    if (is_legacy_wormhole) {
        // Legacy Wormhole: Each channel uses 4 bits.
        for (uint32_t channel = 0; channel < num_dram_channels; ++channel) {
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
    } else {
        // Modern format: Each channel gets 2 bits.
        for (uint32_t channel = 0; channel < num_dram_channels; ++channel) {
            if (telemetry_data & (1 << (2 * channel))) {
                statuses.push_back(DramTrainingStatus::SUCCESS);
            } else if (telemetry_data & (1 << (2 * channel + 1))) {
                statuses.push_back(DramTrainingStatus::FAIL);
            } else {
                statuses.push_back(DramTrainingStatus::IN_PROGRESS);
            }
        }
    }

    return statuses;
}

}  // namespace tt::umd
