// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/firmware_info_provider.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "assert.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/gddr_telemetry.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_dram.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

FirmwareInfoProvider::FirmwareInfoProvider(TTDevice* tt_device) :
    tt_device(tt_device), firmware_version(get_firmware_version_util(tt_device)) {
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    if (telemetry == nullptr) {
        TT_THROW("No telemetry reader present in tt_device.");
    }

    firmware_feature_map = create_firmware_feature_map(tt_device, firmware_version);
}

/* static */ std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProvider::create_firmware_info_provider(
    TTDevice* tt_device) {
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
        case ARCH::BLACKHOLE:
            return std::make_unique<FirmwareInfoProvider>(tt_device);
        default:
            TT_THROW("Unsupported architecture for firmware info provider.");
    }
}

/* static */ FirmwareFeatures FirmwareInfoProvider::create_firmware_feature_map(
    TTDevice* tt_device, const FirmwareBundleVersion& fw_version) {
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            if (fw_version <= FirmwareBundleVersion(18, 3, 0)) {
                return create_wormhole_18_3_base();
            } else if (fw_version <= FirmwareBundleVersion(18, 7, 0)) {
                return create_wormhole_18_4_base();
            }
            return create_wormhole_18_8_base();
        case ARCH::BLACKHOLE:
            if (fw_version <= FirmwareBundleVersion(18, 7, 0)) {
                return create_blackhole_18_5_base();
            }
            return create_blackhole_18_8_base();
        default:
            TT_THROW("Unsupported architecture for telemetry feature map.");
    }
}

// clang-format off
// Base map for all StandardTag firmware versions (18.4+). Does not include MAX_CLOCK_FREQ
// since its source differs per architecture and version.
/* static */ FirmwareFeatures FirmwareInfoProvider::create_18_4_new_telemetry_base() {
    return {
        {FirmwareFeature::ETH_FW_VERSION,    {TelemetryTag::ETH_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::GDDR_FW_VERSION,   {TelemetryTag::GDDR_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::CM_FW_VERSION,     {TelemetryTag::CM_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::DM_APP_FW_VERSION, {TelemetryTag::DM_APP_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::DM_BL_FW_VERSION,  {TelemetryTag::DM_BL_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::TT_FLASH_VERSION,  {TelemetryTag::TT_FLASH_VERSION, LinearTransform{}}},
        {FirmwareFeature::BOARD_ID_HIGH,     {TelemetryTag::BOARD_ID_HIGH, LinearTransform{}}},
        {FirmwareFeature::BOARD_ID_LOW,      {TelemetryTag::BOARD_ID_LOW, LinearTransform{}}},
        {FirmwareFeature::ASIC_LOCATION,     {TelemetryTag::ASIC_LOCATION, LinearTransform{}}},
        {FirmwareFeature::ASIC_TEMPERATURE,  {TelemetryTag::ASIC_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0, NumericSign::SIGNED}}},
        {FirmwareFeature::BOARD_TEMPERATURE, {TelemetryTag::BOARD_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0, NumericSign::SIGNED}}},
        {FirmwareFeature::GDDR_0_1_TEMP,     {TelemetryTag::GDDR_0_1_TEMP, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::GDDR_2_3_TEMP,     {TelemetryTag::GDDR_2_3_TEMP, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::GDDR_4_5_TEMP,     {TelemetryTag::GDDR_4_5_TEMP, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::GDDR_6_7_TEMP,     {TelemetryTag::GDDR_6_7_TEMP, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::MAX_GDDR_TEMP,     {TelemetryTag::MAX_GDDR_TEMP, LinearTransform{}}},
        {FirmwareFeature::AICLK,             {TelemetryTag::AICLK, LinearTransform{}}},
        {FirmwareFeature::AXICLK,            {TelemetryTag::AXICLK, LinearTransform{}}},
        {FirmwareFeature::ARCCLK,            {TelemetryTag::ARCCLK, LinearTransform{}}},
        {FirmwareFeature::DDR_SPEED,         {TelemetryTag::GDDR_SPEED, LinearTransform{}}},
        {FirmwareFeature::TDP,               {TelemetryTag::TDP, LinearTransform{}}},
        {FirmwareFeature::TDC,               {TelemetryTag::TDC, LinearTransform{}}},
        {FirmwareFeature::VCORE,             {TelemetryTag::VCORE, LinearTransform{}}},
        {FirmwareFeature::TDC_LIMIT_MAX,     {TelemetryTag::TDC_LIMIT_MAX, LinearTransform{}}},
        {FirmwareFeature::BOARD_POWER_LIMIT, {TelemetryTag::BOARD_POWER_LIMIT, LinearTransform{}}},
        {FirmwareFeature::FAN_SPEED,         {TelemetryTag::FAN_SPEED, LinearTransform{}}},
        {FirmwareFeature::FAN_RPM,           {TelemetryTag::FAN_RPM, LinearTransform{}}},
        {FirmwareFeature::THM_LIMIT_THROTTLE,{TelemetryTag::THM_LIMIT_THROTTLE, LinearTransform{}}},
        {FirmwareFeature::THM_LIMIT_SHUTDOWN,{TelemetryTag::THM_LIMIT_SHUTDOWN, LinearTransform{}}},
        {FirmwareFeature::DDR_STATUS,        {TelemetryTag::GDDR_STATUS, LinearTransform{}}},
        {FirmwareFeature::HEARTBEAT,         {TelemetryTag::TIMER_HEARTBEAT, LinearTransform{}}},
        {FirmwareFeature::ETH_LIVE_STATUS,   {TelemetryTag::ETH_LIVE_STATUS, LinearTransform{}}},
        {FirmwareFeature::THERM_TRIP_COUNT,  {TelemetryTag::THERM_TRIP_COUNT, LinearTransform{}}},
        {FirmwareFeature::GDDR_UNCORR_ERRS,   {TelemetryTag::GDDR_UNCORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_0_1_CORR_ERRS, {TelemetryTag::GDDR_0_1_CORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_2_3_CORR_ERRS, {TelemetryTag::GDDR_2_3_CORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_4_5_CORR_ERRS, {TelemetryTag::GDDR_4_5_CORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_6_7_CORR_ERRS, {TelemetryTag::GDDR_6_7_CORR_ERRS, LinearTransform{}}},
    };
}

// clang-format on

// clang-format off
// Create base map for legacy Wormhole 18.3 firmware (WormholeTag).
/* static */ FirmwareFeatures FirmwareInfoProvider::create_wormhole_18_3_base() {
    return {
        {FirmwareFeature::ETH_FW_VERSION, {WormholeTag::ETH_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::GDDR_FW_VERSION, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::CM_FW_VERSION, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::DM_APP_FW_VERSION, {WormholeTag::DM_APP_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::DM_BL_FW_VERSION, {WormholeTag::DM_BL_FW_VERSION, LinearTransform{}}},
        {FirmwareFeature::TT_FLASH_VERSION, {WormholeTag::TT_FLASH_VERSION, LinearTransform{}}},
        {FirmwareFeature::BOARD_ID_HIGH, {WormholeTag::BOARD_ID_HIGH, LinearTransform{}}},
        {FirmwareFeature::BOARD_ID_LOW, {WormholeTag::BOARD_ID_LOW, LinearTransform{}}},
        {FirmwareFeature::ASIC_LOCATION, {FixedValue{0}, LinearTransform{}}},
        {FirmwareFeature::ASIC_TEMPERATURE,  {WormholeTag::ASIC_TEMPERATURE,  LinearTransform{0, 0xFFFF,     1.0 / 16.0,    0.0}}},
        {FirmwareFeature::BOARD_TEMPERATURE, {WormholeTag::BOARD_TEMPERATURE, LinearTransform{0, 0xFFFFFFFF, 1.0 / 65536.0, 0.0}}},
        {FirmwareFeature::GDDR_0_1_TEMP, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_2_3_TEMP, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_4_5_TEMP, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_6_7_TEMP, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::MAX_GDDR_TEMP, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::AICLK, {WormholeTag::AICLK, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::AXICLK, {WormholeTag::AXICLK, LinearTransform{}}},
        {FirmwareFeature::ARCCLK, {WormholeTag::ARCCLK, LinearTransform{}}},
        {FirmwareFeature::MAX_CLOCK_FREQ, {SmBusTag{WormholeTag::AICLK}, LinearTransform{16, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::DDR_SPEED, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::TDP, {WormholeTag::TDP, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::TDC, {WormholeTag::TDC, LinearTransform{0, 0xFFFF, 1.0, 0.0}}},
        {FirmwareFeature::VCORE, {WormholeTag::VCORE, LinearTransform{}}},
        {FirmwareFeature::TDC_LIMIT_MAX, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::BOARD_POWER_LIMIT, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::FAN_SPEED, {WormholeTag::FAN_SPEED, LinearTransform{}}},
        {FirmwareFeature::FAN_RPM, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::THM_LIMIT_THROTTLE, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::THM_LIMIT_SHUTDOWN, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::DDR_STATUS, {WormholeTag::DDR_STATUS, LinearTransform{}}},
        {FirmwareFeature::HEARTBEAT, {WormholeTag::ARC0_HEALTH, LinearTransform{}}},
        {FirmwareFeature::ETH_LIVE_STATUS,    {WormholeTag::ETH_LIVE_STATUS, LinearTransform{}}},
        {FirmwareFeature::THERM_TRIP_COUNT, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_UNCORR_ERRS, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_0_1_CORR_ERRS, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_2_3_CORR_ERRS, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_4_5_CORR_ERRS, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::GDDR_6_7_CORR_ERRS, {FixedValue{0}, NotAvailable{}}},
    };
}

// clang-format on

// Wormhole 18.4-18.7: StandardTag base, but MAX_CLOCK_FREQ read via SMBus.
/* static */ FirmwareFeatures FirmwareInfoProvider::create_wormhole_18_4_base() {
    FirmwareFeatures map = create_18_4_new_telemetry_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {SmBusTag{WormholeTag::AICLK}, LinearTransform{16, 0xFFFF, 1.0, 0.0}};
    return map;
}

// Blackhole 18.5-18.7: StandardTag base, but fixed AICLK and no ETH support.
/* static */ FirmwareFeatures FirmwareInfoProvider::create_blackhole_18_5_base() {
    FirmwareFeatures map = create_18_4_new_telemetry_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {FixedValue{blackhole::AICLK_BUSY_VAL}, LinearTransform{}};
    // ETH_FW_VERSION telemetry tag exists but firmware doesn't implement it on Blackhole.
    map[FirmwareFeature::ETH_FW_VERSION] = {FixedValue{0}, NotAvailable{}};
    // ETH_LIVE_STATUS tag exists but always returns zeros on Blackhole.
    map[FirmwareFeature::ETH_LIVE_STATUS] = {FixedValue{0}, NotAvailable{}};
    return map;
}

// Wormhole > 18.7: StandardTag base with StandardTag MAX_CLOCK_FREQ.
/* static */ FirmwareFeatures FirmwareInfoProvider::create_wormhole_18_8_base() {
    FirmwareFeatures map = create_18_4_new_telemetry_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {TelemetryTag::AICLK_LIMIT_MAX, LinearTransform{}};
    return map;
}

// Blackhole > 18.7: StandardTag base with StandardTag MAX_CLOCK_FREQ, no ETH support.
/* static */ FirmwareFeatures FirmwareInfoProvider::create_blackhole_18_8_base() {
    FirmwareFeatures map = create_18_4_new_telemetry_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {TelemetryTag::AICLK_LIMIT_MAX, LinearTransform{}};
    // ETH_FW_VERSION telemetry tag exists but firmware doesn't implement it on Blackhole.
    map[FirmwareFeature::ETH_FW_VERSION] = {FixedValue{0}, NotAvailable{}};
    // ETH_LIVE_STATUS tag exists but always returns zeros on Blackhole.
    map[FirmwareFeature::ETH_LIVE_STATUS] = {FixedValue{0}, NotAvailable{}};
    return map;
}

uint32_t FirmwareInfoProvider::read_raw_telemetry(const FeatureKey& key) const {
    // std::visit + if-constexpr generates a compile-time dispatch equivalent to having separate
    // overloads for each FeatureKey type (StandardTag, WormholeTag, SmBusTag, FixedValue).
    // Using a variant instead of overloads lets us store heterogeneous keys in a single map
    // and iterate over all features uniformly regardless of their underlying read mechanism.
    return std::visit(
        [this](auto&& arg) -> uint32_t {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, StandardTag> || std::is_same_v<T, WormholeTag>) {
                auto* tel = tt_device->get_arc_telemetry_reader();
                return (tel && tel->is_entry_available(arg)) ? tel->read_entry(arg) : 0;
            } else if constexpr (std::is_same_v<T, SmBusTag>) {
                const auto sm_bus_telemetry = std::make_unique<SmBusArcTelemetryReader>(tt_device);
                return sm_bus_telemetry->read_entry(arg.tag);
            } else if constexpr (std::is_same_v<T, FixedValue>) {
                return arg.value;
            }
            return 0u;
        },
        key);
}

bool FirmwareInfoProvider::is_feature_available(FirmwareFeature feature) const {
    auto it = firmware_feature_map.find(feature);
    if (it == firmware_feature_map.end()) {
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

            if constexpr (std::is_same_v<T, StandardTag> || std::is_same_v<T, WormholeTag>) {
                auto* tel = tt_device->get_arc_telemetry_reader();
                return tel && tel->is_entry_available(arg);
            } else if constexpr (std::is_same_v<T, SmBusTag>) {
                const auto sm_bus_telemetry = std::make_unique<SmBusArcTelemetryReader>(tt_device);
                return sm_bus_telemetry->is_entry_available(arg.tag);
            } else if constexpr (std::is_same_v<T, FixedValue>) {
                return true;
            }
            return false;
        },
        it->second.key);
}

template <typename T>
std::optional<T> FirmwareInfoProvider::read_scalar(FirmwareFeature feature) const {
    if (!is_feature_available(feature)) {
        return std::nullopt;
    }

    auto it = firmware_feature_map.find(feature);

    uint32_t raw = read_raw_telemetry(it->second.key);

    // Apply the converter.
    return std::visit(
        [raw](auto&& converter) -> std::optional<T> {
            using C = std::decay_t<decltype(converter)>;

            if constexpr (std::is_same_v<C, LinearTransform>) {
                uint32_t masked = (raw >> converter.shift) & converter.mask;
                double value = converter.signedness == NumericSign::SIGNED
                                   ? static_cast<double>(static_cast<int32_t>(masked))
                                   : static_cast<double>(masked);
                double result = value * converter.scale + converter.offset;
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
template std::optional<uint16_t> FirmwareInfoProvider::read_scalar<uint16_t>(FirmwareFeature feature) const;

FirmwareBundleVersion FirmwareInfoProvider::get_firmware_version() const { return firmware_version; }

/* static */ FirmwareBundleVersion FirmwareInfoProvider::get_latest_supported_firmware_version(tt::ARCH arch) {
    return FirmwareBundleVersion(19, 5, 0);
}

/* static */ FirmwareBundleVersion FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            return FirmwareBundleVersion(18, 3, 0);
        }
        case tt::ARCH::BLACKHOLE: {
            return FirmwareBundleVersion(18, 5, 0);
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

std::optional<uint32_t> FirmwareInfoProvider::get_eth_fw_version() const {
    return read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
}

std::optional<SemVer> FirmwareInfoProvider::get_eth_fw_version_semver() const {
    auto tag_value = get_eth_fw_version();
    if (!tag_value.has_value()) {
        return std::nullopt;
    }
    // Return early if tag value is 0, meaning no ETH cores on chip or version not populated.
    if (tag_value.value() == 0) {
        return std::nullopt;
    }
    switch (tt_device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            return SemVer::from_wormhole_eth_firmware_tag(tag_value.value());
        case tt::ARCH::BLACKHOLE:
            return SemVer::from_blackhole_eth_firmware_tag(tag_value.value());
        default:
            return std::nullopt;
    }
}

std::optional<SemVer> FirmwareInfoProvider::get_gddr_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::GDDR_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_gddr_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_cm_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::CM_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_cm_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_dm_app_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::DM_APP_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_app_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_dm_bl_fw_version() const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::DM_BL_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_bl_fw_version_from_telemetry(*raw, tt_device->get_arch());
}

std::optional<SemVer> FirmwareInfoProvider::get_tt_flash_version() const {
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

std::optional<uint32_t> FirmwareInfoProvider::get_fan_rpm() const {
    auto fan_speed = read_scalar<uint32_t>(FirmwareFeature::FAN_RPM);
    // All ones mean fans not present on board, or not under control of firmware.
    if (fan_speed.has_value() && fan_speed.value() == 0xFFFFFFFF) {
        return std::nullopt;
    }
    return fan_speed;
}

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
    auto telemetry_data = read_scalar<uint32_t>(FirmwareFeature::DDR_STATUS);
    if (!telemetry_data.has_value()) {
        return {};
    }

    // Check if we're using legacy Wormhole format (4 bits per channel)
    // or modern format (2 bits per channel).
    bool is_legacy_wormhole =
        tt_device->get_arch() == ARCH::WORMHOLE_B0 && firmware_version <= FirmwareBundleVersion(18, 3, 0);

    return is_legacy_wormhole ? get_legacy_wormhole_dram_statuses(telemetry_data.value(), num_dram_channels)
                              : get_modern_dram_statuses(telemetry_data.value(), num_dram_channels);
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
    return read_scalar<uint16_t>(FirmwareFeature::DDR_SPEED);
}

std::optional<double> FirmwareInfoProvider::get_current_max_dram_temperature() const {
    return read_scalar<double>(FirmwareFeature::MAX_GDDR_TEMP);
}

std::optional<double> FirmwareInfoProvider::get_thm_limit_shutdown() const {
    // Stored as a plain integer in degrees Celsius.
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_SHUTDOWN);
}

std::optional<uint32_t> FirmwareInfoProvider::get_board_power_limit() const {
    return read_scalar<uint32_t>(FirmwareFeature::BOARD_POWER_LIMIT);
}

std::optional<double> FirmwareInfoProvider::get_thm_limit_throttle() const {
    // Stored as a plain integer in degrees Celsius.
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_THROTTLE);
}

std::optional<uint32_t> FirmwareInfoProvider::get_therm_trip_count() const {
    return read_scalar<uint32_t>(FirmwareFeature::THERM_TRIP_COUNT);
}

/* static */ std::vector<bool> FirmwareInfoProvider::parse_eth_status_bitmask(uint16_t bitmask) {
    static constexpr uint32_t max_eth_links = 16;
    std::vector<bool> statuses(max_eth_links);
    for (uint32_t link = 0; link < max_eth_links; ++link) {
        statuses[link] = static_cast<bool>(bitmask & (1u << link));
    }
    return statuses;
}

std::optional<std::vector<bool>> FirmwareInfoProvider::get_eth_heartbeat_status() const {
    if (!is_feature_available(FirmwareFeature::ETH_LIVE_STATUS)) {
        return std::nullopt;
    }
    uint32_t data = read_raw_telemetry(firmware_feature_map.at(FirmwareFeature::ETH_LIVE_STATUS).key);
    return parse_eth_status_bitmask(static_cast<uint16_t>(data & 0xFFFF));
}

std::optional<std::vector<bool>> FirmwareInfoProvider::get_eth_retrain_status() const {
    if (!is_feature_available(FirmwareFeature::ETH_LIVE_STATUS)) {
        return std::nullopt;
    }
    uint32_t data = read_raw_telemetry(firmware_feature_map.at(FirmwareFeature::ETH_LIVE_STATUS).key);
    return parse_eth_status_bitmask(static_cast<uint16_t>((data >> 16) & 0xFFFF));
}

}  // namespace tt::umd
