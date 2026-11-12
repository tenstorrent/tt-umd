// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "tt-umd/arc/arc_telemetry_reader.hpp"
#include "tt-umd/arc/firmware_telemetry_reader.hpp"
#include "tt-umd/arc/smbus_arc_telemetry_reader.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/firmware/firmware_telemetry_mapping.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/gddr_telemetry.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/types/wormhole_telemetry.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/semver.hpp"
#include "types/wormhole_dram.hpp"

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
    firmware_version(firmware_version) {
    if (telemetry_ == nullptr) {
        UMD_THROW(error::RuntimeError, "FirmwareTelemetryReader cannot be nullptr.");
    }
    if (device_protocol_ == nullptr) {
        UMD_THROW(error::RuntimeError, "DeviceProtocol cannot be nullptr.");
    }

    firmware_feature_map = create_firmware_feature_map(arch, firmware_version);
}

/* static */ std::unique_ptr<FirmwareInfoProvider> FirmwareInfoProviderImplementation::create_firmware_info_provider(
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    FirmwareTelemetryReader* telemetry,
    std::optional<uint64_t> legacy_telemetry_noc_addr) {
    if (telemetry == nullptr) {
        UMD_THROW(error::RuntimeError, "FirmwareTelemetryReader cannot be nullptr.");
    }
    if (device_protocol == nullptr) {
        UMD_THROW(error::RuntimeError, "DeviceProtocol cannot be nullptr.");
    }

    FirmwareBundleVersion firmware_version = FirmwareBundleVersion(0, 0, 0);
    if (dynamic_cast<SmBusArcTelemetryReader*>(telemetry) != nullptr) {
        firmware_version = FirmwareBundleVersion::from_firmware_bundle_tag(
            telemetry->read_entry(wormhole::LegacyTelemetryTag::FW_BUNDLE_VERSION));
    } else if (telemetry->is_entry_available(TelemetryTag::FLASH_BUNDLE_VERSION)) {
        firmware_version =
            FirmwareBundleVersion::from_firmware_bundle_tag(telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION));
    }

    switch (arch) {
        case ARCH::WORMHOLE_B0:
        case ARCH::BLACKHOLE:
            return std::make_unique<FirmwareInfoProviderImplementation>(
                firmware_version,
                arch,
                device_protocol,
                arc_core_noc0,
                arc_core_noc1,
                telemetry,
                legacy_telemetry_noc_addr);
        default:
            UMD_THROW(error::RuntimeError, "Unsupported architecture for FirmwareInfoProvider.");
    }
}

/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_firmware_feature_map(
    tt::ARCH arch, const FirmwareBundleVersion& fw_version) {
    switch (arch) {
        case ARCH::WORMHOLE_B0:
            if (fw_version <= FirmwareBundleVersion(18, 3, 0)) {
                return create_wormhole_18_3_base();
            } else if (fw_version <= FirmwareBundleVersion(18, 7, 0)) {
                return create_wormhole_18_4_base();
            } else if (fw_version < FirmwareBundleVersion(19, 9, 0)) {
                return create_wormhole_18_8_base();
            }
            return create_wormhole_19_9_base();
        case ARCH::BLACKHOLE:
            if (fw_version <= FirmwareBundleVersion(18, 7, 0)) {
                return create_blackhole_18_5_base();
            } else if (fw_version < FirmwareBundleVersion(19, 5, 0)) {
                return create_blackhole_18_8_base();
            } else if (fw_version < FirmwareBundleVersion(19, 7, 0)) {
                return create_blackhole_19_5_base();
            } else if (fw_version < FirmwareBundleVersion(19, 8, 0)) {
                return create_blackhole_19_7_base();
            } else if (fw_version < FirmwareBundleVersion(19, 9, 0)) {
                return create_blackhole_19_8_base();
            } else if (fw_version < FirmwareBundleVersion(19, 12, 0)) {
                return create_blackhole_19_9_base();
            } else if (fw_version < FirmwareBundleVersion(19, 13, 0)) {
                return create_blackhole_19_12_base();
            }
            return create_blackhole_19_13_base();
        default:
            UMD_THROW(error::RuntimeError, "Unsupported architecture for telemetry feature map.");
    }
}

// clang-format off
// Base map for all StandardTag firmware versions (18.4+). Does not include MAX_CLOCK_FREQ
// since its source differs per architecture and version.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_18_4_new_telemetry_base() {
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
        {FirmwareFeature::TDP_LIMIT_MAX,     {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::BOARD_POWER_LIMIT, {TelemetryTag::BOARD_POWER_LIMIT, LinearTransform{}}},
        {FirmwareFeature::FAN_SPEED,         {TelemetryTag::FAN_SPEED, LinearTransform{}}},
        {FirmwareFeature::FAN_RPM,           {TelemetryTag::FAN_RPM, LinearTransform{}}},
        {FirmwareFeature::THM_LIMIT_THROTTLE,{TelemetryTag::THM_LIMIT_THROTTLE, LinearTransform{}}},
        {FirmwareFeature::THM_LIMIT_SHUTDOWN,{TelemetryTag::THM_LIMIT_SHUTDOWN, LinearTransform{}}},
        {FirmwareFeature::DDR_STATUS,        {TelemetryTag::GDDR_STATUS, LinearTransform{}}},
        {FirmwareFeature::HEARTBEAT,         {TelemetryTag::TIMER_HEARTBEAT, LinearTransform{}}},
        {FirmwareFeature::ETH_HEARTBEAT_STATUS, {TelemetryTag::ETH_LIVE_STATUS, LinearTransform{0, 0xFFFF}}},
        {FirmwareFeature::ETH_RETRAIN_STATUS,   {TelemetryTag::ETH_LIVE_STATUS, LinearTransform{16, 0xFFFF}}},
        {FirmwareFeature::ETH_LINK_STATUS,      {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::THERM_TRIP_COUNT,  {TelemetryTag::THERM_TRIP_COUNT, LinearTransform{}}},
        {FirmwareFeature::GDDR_UNCORR_ERRS,   {TelemetryTag::GDDR_UNCORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_0_1_CORR_ERRS, {TelemetryTag::GDDR_0_1_CORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_2_3_CORR_ERRS, {TelemetryTag::GDDR_2_3_CORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_4_5_CORR_ERRS, {TelemetryTag::GDDR_4_5_CORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::GDDR_6_7_CORR_ERRS, {TelemetryTag::GDDR_6_7_CORR_ERRS, LinearTransform{}}},
        {FirmwareFeature::VDD_MAX,            {TelemetryTag::VDD_LIMITS, LinearTransform{16, 0xFFFF}}},
        {FirmwareFeature::VDD_MIN,            {TelemetryTag::VDD_LIMITS, LinearTransform{0, 0xFFFF}}},
        {FirmwareFeature::ENABLED_TENSIX_COL, {TelemetryTag::ENABLED_TENSIX_COL, LinearTransform{}}},
        {FirmwareFeature::ENABLED_TENSIX_ROW, {TelemetryTag::ENABLED_TENSIX_ROW, LinearTransform{}}},
        {FirmwareFeature::ENABLED_ETH,        {TelemetryTag::ENABLED_ETH, LinearTransform{}}},
        {FirmwareFeature::ENABLED_GDDR,       {TelemetryTag::ENABLED_GDDR, LinearTransform{}}},
        {FirmwareFeature::PCIE_0_MODE,        {TelemetryTag::PCIE_USAGE, LinearTransform{0, 0x3}}},
        {FirmwareFeature::PCIE_1_MODE,        {TelemetryTag::PCIE_USAGE, LinearTransform{2, 0x3}}},
        {FirmwareFeature::INPUT_POWER,        {TelemetryTag::INPUT_POWER, LinearTransform{}}},
    };
}

// clang-format on

// clang-format off
// Create base map for legacy Wormhole 18.3 firmware (WormholeTag).
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_18_3_base() {
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
        {FirmwareFeature::ASIC_TEMPERATURE,  {WormholeTag::ASIC_TEMPERATURE,  LinearTransform{0, 0xFFFF, 1.0 / 16.0, 0.0}}},
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
        {FirmwareFeature::TDP_LIMIT_MAX, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::BOARD_POWER_LIMIT, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::FAN_SPEED, {WormholeTag::FAN_SPEED, LinearTransform{}}},
        {FirmwareFeature::FAN_RPM, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::THM_LIMIT_THROTTLE, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::THM_LIMIT_SHUTDOWN, {FixedValue{0}, NotAvailable{}}},
        {FirmwareFeature::DDR_STATUS, {WormholeTag::DDR_STATUS, LinearTransform{}}},
        {FirmwareFeature::HEARTBEAT, {WormholeTag::ARC0_HEALTH, LinearTransform{}}},
        {FirmwareFeature::ETH_HEARTBEAT_STATUS, {WormholeTag::ETH_LIVE_STATUS, LinearTransform{0, 0xFFFF}}},
        {FirmwareFeature::ETH_RETRAIN_STATUS,   {WormholeTag::ETH_LIVE_STATUS, LinearTransform{16, 0xFFFF}}},
        {FirmwareFeature::ETH_LINK_STATUS,      {FixedValue{0}, NotAvailable{}}},
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
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_18_4_base() {
    FirmwareFeatures map = create_18_4_new_telemetry_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {SmBusTag{WormholeTag::AICLK}, LinearTransform{16, 0xFFFF, 1.0, 0.0}};
    // Wormhole publishes the TDP limit from the new telemetry onwards.
    map[FirmwareFeature::TDP_LIMIT_MAX] = {TelemetryTag::TDP_LIMIT_MAX, LinearTransform{}};
    return map;
}

// Wormhole 18.8-19.8: same as 18.4, except MAX_CLOCK_FREQ moves from SMBus to a telemetry tag.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_18_8_base() {
    FirmwareFeatures map = create_wormhole_18_4_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {TelemetryTag::AICLK_LIMIT_MAX, LinearTransform{}};
    return map;
}

// Wormhole >= 19.9: ETH_LIVE_STATUS upper 16 bits change from retrain to link status.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_wormhole_19_9_base() {
    FirmwareFeatures map = create_wormhole_18_8_base();
    map[FirmwareFeature::ETH_RETRAIN_STATUS] = {FixedValue{0}, NotAvailable{}};
    map[FirmwareFeature::ETH_LINK_STATUS] = {TelemetryTag::ETH_LIVE_STATUS, LinearTransform{16, 0xFFFF}};
    return map;
}

// Blackhole 18.5-18.7: StandardTag base, but fixed AICLK and no ETH support.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_5_base() {
    FirmwareFeatures map = create_18_4_new_telemetry_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {FixedValue{blackhole::AICLK_BUSY_VAL}, LinearTransform{}};
    // ETH_FW_VERSION telemetry tag exists but firmware doesn't implement it on Blackhole.
    map[FirmwareFeature::ETH_FW_VERSION] = {FixedValue{0}, NotAvailable{}};
    // ETH_LIVE_STATUS tag exists but always returns zeros on Blackhole prior to 19.9.
    map[FirmwareFeature::ETH_HEARTBEAT_STATUS] = {FixedValue{0}, NotAvailable{}};
    map[FirmwareFeature::ETH_RETRAIN_STATUS] = {FixedValue{0}, NotAvailable{}};
    // Blackhole harvests Tensix by column, so there is no row mask.
    map[FirmwareFeature::ENABLED_TENSIX_ROW] = {FixedValue{0}, NotAvailable{}};
    map[FirmwareFeature::ENABLED_L2CPU] = {TelemetryTag::ENABLED_L2CPU, LinearTransform{}};
    // The upper 16 bits of the L2CPU clock tags are reserved for a maximum frequency.
    map[FirmwareFeature::L2CPUCLK0] = {TelemetryTag::L2CPUCLK0, LinearTransform{0, 0xFFFF}};
    map[FirmwareFeature::L2CPUCLK1] = {TelemetryTag::L2CPUCLK1, LinearTransform{0, 0xFFFF}};
    map[FirmwareFeature::L2CPUCLK2] = {TelemetryTag::L2CPUCLK2, LinearTransform{0, 0xFFFF}};
    map[FirmwareFeature::L2CPUCLK3] = {TelemetryTag::L2CPUCLK3, LinearTransform{0, 0xFFFF}};
    return map;
}

// Blackhole 18.8-19.4: same as 18.5, except MAX_CLOCK_FREQ moves from a fixed value to a telemetry tag.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_18_8_base() {
    FirmwareFeatures map = create_blackhole_18_5_base();
    map[FirmwareFeature::MAX_CLOCK_FREQ] = {TelemetryTag::AICLK_LIMIT_MAX, LinearTransform{}};
    return map;
}

// Blackhole 19.5-19.6: AICLK arbitration tags appear. The ARB_MIN/ARB_MAX tags hold only the frequency.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_5_base() {
    FirmwareFeatures map = create_blackhole_18_8_base();
    map[FirmwareFeature::AICLK_ARB_MIN_FREQ] = {TelemetryTag::AICLK_ARB_MIN, LinearTransform{0, 0xFFFF}};
    map[FirmwareFeature::AICLK_ARB_MAX_FREQ] = {TelemetryTag::AICLK_ARB_MAX, LinearTransform{0, 0xFFFF}};
    map[FirmwareFeature::ENABLED_AICLK_MIN_ARBITERS] = {TelemetryTag::ENABLED_MIN_ARB, LinearTransform{}};
    map[FirmwareFeature::ENABLED_AICLK_MAX_ARBITERS] = {TelemetryTag::ENABLED_MAX_ARB, LinearTransform{}};
    return map;
}

// Blackhole 19.7: the winning arbiter is packed above the frequency, and AICLK_PPM_INFO appears.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_7_base() {
    FirmwareFeatures map = create_blackhole_19_5_base();
    map[FirmwareFeature::AICLK_ARB_MIN_ARBITER] = {TelemetryTag::AICLK_ARB_MIN, LinearTransform{16, 0xFFFF}};
    map[FirmwareFeature::AICLK_ARB_MAX_ARBITER] = {TelemetryTag::AICLK_ARB_MAX, LinearTransform{16, 0xFFFF}};
    // AICLK_PPM_INFO packs the fields the other way round from ARB_MIN/ARB_MAX: arbiter low, reason high.
    map[FirmwareFeature::AICLK_TARGET_ARBITER] = {TelemetryTag::AICLK_PPM_INFO, LinearTransform{0, 0xFFFF}};
    map[FirmwareFeature::AICLK_TARGET_REASON] = {TelemetryTag::AICLK_PPM_INFO, LinearTransform{16, 0xFFFF}};
    return map;
}

// Blackhole 19.8: firmware gains the adjustable TDP limit and the host AICLK ceiling, and publishes both.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_8_base() {
    FirmwareFeatures map = create_blackhole_19_7_base();
    map[FirmwareFeature::TDP_LIMIT_MAX] = {TelemetryTag::TDP_LIMIT_MAX, LinearTransform{}};
    map[FirmwareFeature::HOST_AICLK_LIMIT] = {TelemetryTag::HOST_AICLK_LIMIT, LinearTransform{}};
    return map;
}

// Blackhole >= 19.9: ETH_LIVE_STATUS becomes available (upper 16 = link, lower 16 = heartbeat).
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_9_base() {
    FirmwareFeatures map = create_blackhole_19_8_base();
    map[FirmwareFeature::ETH_HEARTBEAT_STATUS] = {TelemetryTag::ETH_LIVE_STATUS, LinearTransform{0, 0xFFFF}};
    map[FirmwareFeature::ETH_LINK_STATUS] = {TelemetryTag::ETH_LIVE_STATUS, LinearTransform{16, 0xFFFF}};
    return map;
}

// Blackhole 19.12: SMBus error count, GDDR IO rail power and the kernel NOP throttler appear.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_12_base() {
    FirmwareFeatures map = create_blackhole_19_9_base();
    map[FirmwareFeature::SMBUS_ERRORS] = {TelemetryTag::SMBUS_ERRORS, LinearTransform{}};
    map[FirmwareFeature::GDDR_WEST_IO_POWER] = {TelemetryTag::GDDR_WEST_IO_POWER, LinearTransform{}};
    map[FirmwareFeature::GDDR_EAST_IO_POWER] = {TelemetryTag::GDDR_EAST_IO_POWER, LinearTransform{}};
    map[FirmwareFeature::NOP_START_COUNT] = {TelemetryTag::NOP_START_COUNT, LinearTransform{}};
    map[FirmwareFeature::NOP_ON_DURATION] = {TelemetryTag::NOP_ON_DURATION, LinearTransform{}};
    // There is no capability flag yet; every 19.12 firmware ships the throttler.
    map[FirmwareFeature::KERNEL_NOP_THROTTLER_SUPPORTED] = {FixedValue{1}, LinearTransform{}};
    map[FirmwareFeature::KERNEL_NOP_THROTTLER_ENABLED] = {TelemetryTag::KERNEL_THROTTLER, LinearTransform{0, 0x1}};
    map[FirmwareFeature::KERNEL_NOP_THROTTLER_STOP_FREQ] = {
        TelemetryTag::KERNEL_THROTTLER, LinearTransform{16, 0xFFFF}};
    return map;
}

// Blackhole >= 19.13: firmware publishes feature capability and active-config flags, and the kernel NOP throttler's
// settings are only meaningful while the firmware reports the throttler as supported. The NOP counters are not gated:
// they also count NOPs from critical di/dt throttling, which runs regardless of the throttler.
/* static */ FirmwareFeatures FirmwareInfoProviderImplementation::create_blackhole_19_13_base() {
    FirmwareFeatures map = create_blackhole_19_12_base();
    // A capability's bit is set in the capability word when it is supported and in the active-config word when it is
    // enabled; both words use the same layout: bit 0 is the kernel NOP throttler and bit 1 the GDDR thermal-trip action.
    // An enabled flag only means something while the capability is supported.
    map[FirmwareFeature::KERNEL_NOP_THROTTLER_SUPPORTED] = {TelemetryTag::FW_CAPABILITIES_0, LinearTransform{0, 0x1}};
    map[FirmwareFeature::KERNEL_NOP_THROTTLER_ENABLED] = {
        TelemetryTag::FW_ACTIVE_CONFIG_0, LinearTransform{0, 0x1}, FirmwareFeature::KERNEL_NOP_THROTTLER_SUPPORTED};
    map[FirmwareFeature::GDDR_THERMAL_TRIP_ACTION_SUPPORTED] = {
        TelemetryTag::FW_CAPABILITIES_0, LinearTransform{1, 0x1}};
    map[FirmwareFeature::GDDR_THERMAL_TRIP_ACTION_ENABLED] = {
        TelemetryTag::FW_ACTIVE_CONFIG_0,
        LinearTransform{1, 0x1},
        FirmwareFeature::GDDR_THERMAL_TRIP_ACTION_SUPPORTED};
    map[FirmwareFeature::KERNEL_NOP_THROTTLER_STOP_FREQ] = {
        TelemetryTag::KERNEL_THROTTLER, LinearTransform{16, 0xFFFF}, FirmwareFeature::KERNEL_NOP_THROTTLER_SUPPORTED};
    return map;
}

uint32_t FirmwareInfoProviderImplementation::read_raw_telemetry(const FeatureKey& key) const {
    // std::visit + if-constexpr generates a compile-time dispatch equivalent to having separate
    // overloads for each FeatureKey type (StandardTag, WormholeTag, SmBusTag, FixedValue).
    // Using a variant instead of overloads lets us store heterogeneous keys in a single map
    // and iterate over all features uniformly regardless of their underlying read mechanism.
    return std::visit(
        [this](auto&& arg) -> uint32_t {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, StandardTag> || std::is_same_v<T, WormholeTag>) {
                return (telemetry_ && telemetry_->is_entry_available(arg)) ? telemetry_->read_entry(arg) : 0;
            } else if constexpr (std::is_same_v<T, SmBusTag>) {
                auto sm_bus_telemetry = std::make_unique<SmBusArcTelemetryReader>(
                    device_protocol_, arc_core_noc0_, arc_core_noc1_, legacy_telemetry_noc_addr_);
                return sm_bus_telemetry->read_entry(arg.tag);
            } else if constexpr (std::is_same_v<T, FixedValue>) {
                return arg.value;
            }
            return 0u;
        },
        key);
}

bool FirmwareInfoProviderImplementation::is_feature_available(FirmwareFeature feature) const {
    auto it = firmware_feature_map.find(feature);
    if (it == firmware_feature_map.end()) {
        return false;
    }

    // Check if converter indicates not available.
    if (std::holds_alternative<NotAvailable>(it->second.converter)) {
        return false;
    }

    if (it->second.gated_by.has_value() && read_scalar<uint32_t>(it->second.gated_by.value()).value_or(0) == 0) {
        return false;
    }

    // Check if key is available in telemetry.
    return std::visit(
        [this](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, StandardTag> || std::is_same_v<T, WormholeTag>) {
                return telemetry_ && telemetry_->is_entry_available(arg);
            } else if constexpr (std::is_same_v<T, SmBusTag>) {
                auto sm_bus_telemetry = std::make_unique<SmBusArcTelemetryReader>(
                    device_protocol_, arc_core_noc0_, arc_core_noc1_, legacy_telemetry_noc_addr_);
                return sm_bus_telemetry->is_entry_available(arg.tag);
            } else if constexpr (std::is_same_v<T, FixedValue>) {
                return true;
            }
            return false;
        },
        it->second.key);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::read_raw(FirmwareFeature feature) const {
    if (!is_feature_available(feature)) {
        return std::nullopt;
    }
    return read_raw_telemetry(firmware_feature_map.find(feature)->second.key);
}

template <typename T>
std::optional<T> FirmwareInfoProviderImplementation::convert_raw(FirmwareFeature feature, uint32_t raw) const {
    auto it = firmware_feature_map.find(feature);
    if (it == firmware_feature_map.end()) {
        return std::nullopt;
    }

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

template <typename T>
std::optional<T> FirmwareInfoProviderImplementation::read_scalar(FirmwareFeature feature) const {
    std::optional<uint32_t> raw = read_raw(feature);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return convert_raw<T>(feature, raw.value());
}

// Explicit instantiations.
template std::optional<uint32_t> FirmwareInfoProviderImplementation::read_scalar<uint32_t>(
    FirmwareFeature feature) const;
template std::optional<double> FirmwareInfoProviderImplementation::read_scalar<double>(FirmwareFeature feature) const;
template std::optional<uint8_t> FirmwareInfoProviderImplementation::read_scalar<uint8_t>(FirmwareFeature feature) const;
template std::optional<uint16_t> FirmwareInfoProviderImplementation::read_scalar<uint16_t>(
    FirmwareFeature feature) const;
template std::optional<bool> FirmwareInfoProviderImplementation::read_scalar<bool>(FirmwareFeature feature) const;

FirmwareBundleVersion FirmwareInfoProviderImplementation::get_firmware_version(NocId noc_id) const {
    return firmware_version;
}

std::optional<uint64_t> FirmwareInfoProviderImplementation::get_board_id(NocId noc_id) const {
    if (!(is_feature_available(FirmwareFeature::BOARD_ID_HIGH) &&
          is_feature_available(FirmwareFeature::BOARD_ID_LOW))) {
        return std::nullopt;
    }
    uint32_t high = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_HIGH).value_or(0);
    uint32_t low = read_scalar<uint32_t>(FirmwareFeature::BOARD_ID_LOW).value_or(0);
    return (static_cast<uint64_t>(high) << 32) | low;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_eth_fw_version(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_eth_fw_version_semver(NocId noc_id) const {
    auto tag_value = read_scalar<uint32_t>(FirmwareFeature::ETH_FW_VERSION);
    if (!tag_value.has_value()) {
        return std::nullopt;
    }
    // Return early if tag value is 0, meaning no ETH cores on chip or version not populated.
    if (tag_value.value() == 0) {
        return std::nullopt;
    }
    switch (arch_) {
        case tt::ARCH::WORMHOLE_B0:
            return SemVer::from_wormhole_eth_firmware_tag(tag_value.value());
        case tt::ARCH::BLACKHOLE:
            return SemVer::from_blackhole_eth_firmware_tag(tag_value.value());
        default:
            return std::nullopt;
    }
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_gddr_fw_version(NocId noc_id) const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::GDDR_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_gddr_fw_version_from_telemetry(*raw, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_cm_fw_version(NocId noc_id) const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::CM_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_cm_fw_version_from_telemetry(*raw, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_app_fw_version(NocId noc_id) const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::DM_APP_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_app_fw_version_from_telemetry(*raw, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_dm_bl_fw_version(NocId noc_id) const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::DM_BL_FW_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_dm_bl_fw_version_from_telemetry(*raw, arch_);
}

std::optional<SemVer> FirmwareInfoProviderImplementation::get_tt_flash_version(NocId noc_id) const {
    auto raw = read_scalar<uint32_t>(FirmwareFeature::TT_FLASH_VERSION);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return get_tt_flash_version_from_telemetry(*raw);
}

std::optional<double> FirmwareInfoProviderImplementation::get_asic_temperature(NocId noc_id) const {
    return read_scalar<double>(FirmwareFeature::ASIC_TEMPERATURE);
}

std::optional<double> FirmwareInfoProviderImplementation::get_board_temperature(NocId noc_id) const {
    return read_scalar<double>(FirmwareFeature::BOARD_TEMPERATURE);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_max_clock_freq(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::MAX_CLOCK_FREQ);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_min_clock_freq(NocId noc_id) const {
    return std::nullopt;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_aiclk(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::AICLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_clock_freq(NocId noc_id) const {
    return get_aiclk(noc_id);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_axiclk(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::AXICLK);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_arcclk(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::ARCCLK);
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_speeds(NocId noc_id) const {
    std::vector<std::optional<uint32_t>> fan_speeds(MAX_NUMBER_OF_FANS, std::nullopt);

    auto fan_speed = read_scalar<uint32_t>(FirmwareFeature::FAN_SPEED);
    // No fan speed information available.
    if (!fan_speed.has_value()) {
        return fan_speeds;
    }

    fan_speeds.at(0) = (fan_speed == 0xFFFFFFFF) ? std::nullopt : std::optional<uint32_t>(fan_speed);
    return fan_speeds;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::TDP);
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_fan_rpms(NocId noc_id) const {
    std::vector<std::optional<uint32_t>> fan_rpms(MAX_NUMBER_OF_FANS, std::nullopt);
    auto fan_rpm = read_scalar<uint32_t>(FirmwareFeature::FAN_RPM);
    // No fan RPM information available.
    if (!fan_rpm.has_value()) {
        return fan_rpms;
    }
    // Since 19.10, values for two fans are packed into one telemetry entry.
    if (arch_ == ARCH::WORMHOLE_B0 && firmware_version >= FirmwareBundleVersion(19, 10, 0)) {
        uint32_t left_fan = fan_rpm.value() >> 16;
        uint32_t right_fan = fan_rpm.value() & 0xFFFF;
        fan_rpms.at(0) = (left_fan == 0xFFFF) ? std::nullopt : std::optional<uint32_t>(left_fan);
        fan_rpms.at(1) = (right_fan == 0xFFFF) ? std::nullopt : std::optional<uint32_t>(right_fan);
        return fan_rpms;
    }

    fan_rpms.at(0) = (fan_rpm == 0xFFFFFFFF) ? std::nullopt : std::optional<uint32_t>(fan_rpm);
    return fan_rpms;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_speed(NocId noc_id) const {
    return get_fan_speeds(noc_id).front();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_fan_rpm(NocId noc_id) const {
    return get_fan_rpms(noc_id).front();
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdc(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::TDC);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_vcore(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::VCORE);
}

std::optional<uint8_t> FirmwareInfoProviderImplementation::get_asic_location(NocId noc_id) const {
    return read_scalar<uint8_t>(FirmwareFeature::ASIC_LOCATION);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_heartbeat(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::HEARTBEAT);
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

// Decode a single channel's 2-bit field. The lower bit reports completion and the higher bit reports
// the error. This layout is shared by the training half ([15:0]) and the BIST half ([31:16]) of the
// GDDR_STATUS telemetry tag.
static DramTrainingStatus decode_dram_status_bits(uint8_t two_bits) {
    switch (two_bits & 0x3) {
        case 0b01:
            return DramTrainingStatus::SUCCESS;
        case 0b10:
        case 0b11:
            return DramTrainingStatus::FAIL;
        default:  // 0b00
            return DramTrainingStatus::IN_PROGRESS;
    }
}

// Modern format: Each channel gets two bits in the 32-bit value.
// The lower bits are for lower channels.
// Lower of the two bits reports the training status and higher of the two bits reports the training error.
// Example: 0b 00 00 00 00 00 00 01 10
// would mean that only channel 0 is trained, channel 1 has the error and other channels are not trained
// and don't have errors. If some channel is harvested the bits are always going to be zero.
//
// As of FW 19.7.0 the GDDR BIST status is reported in the upper 16 bits using the same per-channel
// layout, shifted by 16 (bit [16 + 2*channel] = BIST complete, bit [17 + 2*channel] = BIST failed).
// When check_bist is set, a channel is SUCCESS only if both training and BIST passed, and FAIL if
// either failed, so that a BIST failure also triggers a directed retrain.
static std::vector<DramTrainingStatus> get_modern_dram_statuses(
    uint32_t telemetry_data, uint32_t num_channels, bool check_bist) {
    std::vector<DramTrainingStatus> statuses;
    for (uint32_t channel = 0; channel < num_channels; ++channel) {
        DramTrainingStatus training = decode_dram_status_bits((telemetry_data >> (channel * 2)) & 0x3);

        if (!check_bist) {
            statuses.push_back(training);
            continue;
        }

        DramTrainingStatus bist = decode_dram_status_bits((telemetry_data >> (16 + channel * 2)) & 0x3);

        // Require both training and BIST to pass for SUCCESS; fail (and retrain) if either failed.
        if (training == DramTrainingStatus::FAIL || bist == DramTrainingStatus::FAIL) {
            statuses.push_back(DramTrainingStatus::FAIL);
        } else if (training == DramTrainingStatus::SUCCESS && bist == DramTrainingStatus::SUCCESS) {
            statuses.push_back(DramTrainingStatus::SUCCESS);
        } else {
            statuses.push_back(DramTrainingStatus::IN_PROGRESS);
        }
    }
    return statuses;
}

std::vector<DramTrainingStatus> FirmwareInfoProviderImplementation::get_dram_training_status(
    uint32_t num_dram_channels, NocId noc_id) const {
    auto telemetry_data = read_scalar<uint32_t>(FirmwareFeature::DDR_STATUS);
    if (!telemetry_data.has_value()) {
        return {};
    }

    // Check if we're using legacy Wormhole format (4 bits per channel)
    // or modern format (2 bits per channel).
    bool is_legacy_wormhole = arch_ == ARCH::WORMHOLE_B0 && firmware_version <= FirmwareBundleVersion(18, 3, 0);

    // BIST status is reported in the upper 16 bits of GDDR_STATUS starting with FW 19.7.0. Only
    // Blackhole has GDDR and reports BIST status here, so gate on the architecture as well as the
    // firmware version. Older firmware leaves those bits zero.
    bool check_bist = arch_ == ARCH::BLACKHOLE && firmware_version >= FirmwareBundleVersion(19, 7, 0);

    return is_legacy_wormhole ? get_legacy_wormhole_dram_statuses(telemetry_data.value(), num_dram_channels)
                              : get_modern_dram_statuses(telemetry_data.value(), num_dram_channels, check_bist);
}

static bool gddr_telemetry_tags_available(FirmwareTelemetryReader* telemetry) {
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

std::optional<GddrModuleTelemetry> FirmwareInfoProviderImplementation::get_dram_telemetry(
    GddrModule gddr_module, NocId noc_id) const {
    const uint8_t module_index = static_cast<uint8_t>(gddr_module);
    const uint8_t pair_index = module_index / 2;

    if (!telemetry_->is_entry_available(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) + pair_index) ||
        !telemetry_->is_entry_available(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) + pair_index) ||
        !telemetry_->is_entry_available(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS))) {
        return std::nullopt;
    }

    const uint32_t temp_word = telemetry_->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) + pair_index);
    const uint32_t corr_word =
        telemetry_->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) + pair_index);
    const uint32_t uncorr_bitmask = telemetry_->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS));

    return decode_gddr_module_telemetry(module_index, temp_word, corr_word, uncorr_bitmask);
}

std::optional<GddrTelemetry> FirmwareInfoProviderImplementation::get_aggregated_dram_telemetry(NocId noc_id) const {
    if (!gddr_telemetry_tags_available(telemetry_)) {
        return std::nullopt;
    }

    GddrTelemetry aggregated{};

    const uint32_t uncorr_bitmask = telemetry_->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_UNCORR_ERRS));

    for (uint8_t pair = 0; pair < 4; ++pair) {
        const uint32_t temp_word = telemetry_->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_TEMP) + pair);
        const uint32_t corr_word =
            telemetry_->read_entry(static_cast<uint8_t>(TelemetryTag::GDDR_0_1_CORR_ERRS) + pair);

        const uint8_t even_module = pair * 2;
        const uint8_t odd_module = pair * 2 + 1;

        aggregated.modules[static_cast<GddrModule>(even_module)] =
            decode_gddr_module_telemetry(even_module, temp_word, corr_word, uncorr_bitmask);
        aggregated.modules[static_cast<GddrModule>(odd_module)] =
            decode_gddr_module_telemetry(odd_module, temp_word, corr_word, uncorr_bitmask);
    }

    return aggregated;
}

std::optional<uint16_t> FirmwareInfoProviderImplementation::get_dram_speed(NocId noc_id) const {
    return read_scalar<uint16_t>(FirmwareFeature::DDR_SPEED);
}

std::optional<double> FirmwareInfoProviderImplementation::get_current_max_dram_temperature(NocId noc_id) const {
    return read_scalar<double>(FirmwareFeature::MAX_GDDR_TEMP);
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_shutdown(NocId noc_id) const {
    // Stored as a plain integer in degrees Celsius.
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_SHUTDOWN);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdp_limit(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::TDP_LIMIT_MAX);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_board_power_limit(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::BOARD_POWER_LIMIT);
}

std::optional<double> FirmwareInfoProviderImplementation::get_thm_limit_throttle(NocId noc_id) const {
    // Stored as a plain integer in degrees Celsius.
    return read_scalar<double>(FirmwareFeature::THM_LIMIT_THROTTLE);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_therm_trip_count(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::THERM_TRIP_COUNT);
}

// The firmware bitmask is indexed by physical channel, and ETH_CORES_NOC0 holds the physical coords in that same
// channel order. So bit N maps directly to core N; harvested channels report 0 here and are dropped later by
// filter_harvested_eth_status.
// NOTE: This mapping depends on the ordering of ETH_CORES_NOC0 matching the firmware's physical channel indexing.
// That array is expected to remain stable; reordering it would silently break the bit-to-core mapping.
std::vector<std::pair<CoreCoord, bool>> FirmwareInfoProviderImplementation::parse_eth_status_bitmask(
    uint16_t bitmask) const {
    const std::vector<tt_xy_pair>& eth_cores_noc0 = [this]() -> const std::vector<tt_xy_pair>& {
        switch (arch_) {
            case tt::ARCH::WORMHOLE_B0:
                return wormhole::ETH_CORES_NOC0;
            case tt::ARCH::BLACKHOLE:
                return blackhole::ETH_CORES_NOC0;
            case tt::ARCH::QUASAR:
                return grendel::ETH_CORES_NOC0;
            default:
                UMD_THROW(error::RuntimeError, "ETH status parsing not supported for this architecture.");
        }
    }();

    std::vector<std::pair<CoreCoord, bool>> statuses;
    statuses.reserve(eth_cores_noc0.size());
    for (uint32_t channel = 0; channel < eth_cores_noc0.size(); ++channel) {
        CoreCoord noc0_coord(eth_cores_noc0[channel], CoreType::ETH, CoordSystem::NOC0);
        statuses.emplace_back(noc0_coord, static_cast<bool>(bitmask & (1u << channel)));
    }
    return statuses;
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_heartbeat_status(NocId noc_id) const {
    auto statuses = get_eth_heartbeat_status_per_core(noc_id);
    if (!statuses.has_value()) {
        return std::nullopt;
    }
    std::vector<bool> heartbeat_status;
    heartbeat_status.reserve(statuses->size());
    for (const auto& [core, status] : statuses.value()) {
        heartbeat_status.push_back(status);
    }
    return heartbeat_status;
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_heartbeat_status_per_core(NocId noc_id) const {
    auto data = read_scalar<uint16_t>(FirmwareFeature::ETH_HEARTBEAT_STATUS);
    if (!data.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(data.value());
}

std::optional<std::vector<std::pair<CoreCoord, bool>>> FirmwareInfoProviderImplementation::get_eth_link_status_per_core(
    NocId noc_id) const {
    auto data = read_scalar<uint16_t>(FirmwareFeature::ETH_LINK_STATUS);
    if (!data.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(data.value());
}

std::optional<std::vector<bool>> FirmwareInfoProviderImplementation::get_eth_retrain_status(NocId noc_id) const {
    auto statuses = get_eth_retrain_status_per_core(noc_id);
    if (!statuses.has_value()) {
        return std::nullopt;
    }
    std::vector<bool> retrain_status;
    retrain_status.reserve(statuses->size());
    for (const auto& [core, status] : statuses.value()) {
        retrain_status.push_back(status);
    }
    return retrain_status;
}

std::optional<std::vector<std::pair<CoreCoord, bool>>>
FirmwareInfoProviderImplementation::get_eth_retrain_status_per_core(NocId noc_id) const {
    auto data = read_scalar<uint16_t>(FirmwareFeature::ETH_RETRAIN_STATUS);
    if (!data.has_value()) {
        return std::nullopt;
    }
    return parse_eth_status_bitmask(data.value());
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_vdd_max(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::VDD_MAX);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_vdd_min(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::VDD_MIN);
}

std::vector<std::optional<uint32_t>> FirmwareInfoProviderImplementation::get_l2cpu_clocks(NocId noc_id) const {
    return {
        read_scalar<uint32_t>(FirmwareFeature::L2CPUCLK0),
        read_scalar<uint32_t>(FirmwareFeature::L2CPUCLK1),
        read_scalar<uint32_t>(FirmwareFeature::L2CPUCLK2),
        read_scalar<uint32_t>(FirmwareFeature::L2CPUCLK3),
    };
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_enabled_tensix_columns(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::ENABLED_TENSIX_COL);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_enabled_tensix_rows(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::ENABLED_TENSIX_ROW);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_enabled_eth_cores(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::ENABLED_ETH);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_enabled_gddr(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::ENABLED_GDDR);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_enabled_l2cpus(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::ENABLED_L2CPU);
}

// Converts a raw value to an enum. Values past the last enumerator that UMD knows about, which newer firmware may add,
// become Enum::UNKNOWN, so that switches and string conversions over the enum still handle them. std::nullopt is
// reserved for "the firmware does not publish this value".
template <typename Enum>
static Enum enum_from_raw(uint32_t raw, Enum last_known) {
    using Underlying = std::underlying_type_t<Enum>;
    return raw <= static_cast<uint32_t>(static_cast<Underlying>(last_known)) ? static_cast<Enum>(raw) : Enum::UNKNOWN;
}

template <typename Enum>
static std::optional<Enum> to_enum(std::optional<uint32_t> raw, Enum last_known) {
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return enum_from_raw(raw.value(), last_known);
}

// Converts a bitmask, in which bit i is set when the enumerator with value i is enabled, to a list of enumerators.
// Set bits past the last known enumerator are reported as a single Enum::UNKNOWN.
template <typename Enum>
static std::vector<Enum> enum_list_from_mask(uint32_t mask, Enum last_known) {
    using Underlying = std::underlying_type_t<Enum>;
    const uint32_t last_known_bit = static_cast<uint32_t>(static_cast<Underlying>(last_known));
    std::vector<Enum> enabled;
    bool has_unknown = false;
    for (uint32_t bit = 0; bit < 32; ++bit) {
        if (((mask >> bit) & 1u) == 0) {
            continue;
        }
        if (bit <= last_known_bit) {
            enabled.push_back(static_cast<Enum>(bit));
        } else {
            has_unknown = true;
        }
    }
    if (has_unknown) {
        enabled.push_back(Enum::UNKNOWN);
    }
    return enabled;
}

std::vector<std::optional<PcieMode>> FirmwareInfoProviderImplementation::get_pcie_modes(NocId noc_id) const {
    return {
        to_enum(read_scalar<uint32_t>(FirmwareFeature::PCIE_0_MODE), PcieMode::ROOT_PORT),
        to_enum(read_scalar<uint32_t>(FirmwareFeature::PCIE_1_MODE), PcieMode::ROOT_PORT),
    };
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_input_power(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::INPUT_POWER);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_tdc_limit(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::TDC_LIMIT_MAX);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_gddr_west_io_power(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::GDDR_WEST_IO_POWER);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_gddr_east_io_power(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::GDDR_EAST_IO_POWER);
}

std::optional<AiclkArbitration> FirmwareInfoProviderImplementation::get_aiclk_arbitration(NocId noc_id) const {
    // Read each telemetry word exactly once and decode every field packed into it from that read. ARB_MIN and ARB_MAX
    // each pack a frequency with the arbiter that set it, and PPM_INFO packs the target reason with the target arbiter,
    // so reading them through separate getters could pair a value with one from a different telemetry update. The
    // features sharing a word share its telemetry tag in the feature map.
    const std::optional<uint32_t> min_raw = read_raw(FirmwareFeature::AICLK_ARB_MIN_FREQ);
    const std::optional<uint32_t> max_raw = read_raw(FirmwareFeature::AICLK_ARB_MAX_FREQ);
    const std::optional<uint32_t> enabled_min_raw = read_raw(FirmwareFeature::ENABLED_AICLK_MIN_ARBITERS);
    const std::optional<uint32_t> enabled_max_raw = read_raw(FirmwareFeature::ENABLED_AICLK_MAX_ARBITERS);
    if (!min_raw.has_value() || !max_raw.has_value() || !enabled_min_raw.has_value() || !enabled_max_raw.has_value()) {
        return std::nullopt;
    }

    AiclkArbitration arbitration;
    arbitration.min_freq_mhz = convert_raw<uint32_t>(FirmwareFeature::AICLK_ARB_MIN_FREQ, min_raw.value()).value();
    arbitration.max_freq_mhz = convert_raw<uint32_t>(FirmwareFeature::AICLK_ARB_MAX_FREQ, max_raw.value()).value();
    arbitration.enabled_min_arbiters = enum_list_from_mask(
        convert_raw<uint32_t>(FirmwareFeature::ENABLED_AICLK_MIN_ARBITERS, enabled_min_raw.value()).value(),
        AiclkMinArbiter::BUSY);
    arbitration.enabled_max_arbiters = enum_list_from_mask(
        convert_raw<uint32_t>(FirmwareFeature::ENABLED_AICLK_MAX_ARBITERS, enabled_max_raw.value()).value(),
        AiclkMaxArbiter::HOST_FMAX);

    // The winning arbiters and the target are published from firmware 19.7.
    if (is_feature_available(FirmwareFeature::AICLK_ARB_MIN_ARBITER)) {
        arbitration.min_arbiter = to_enum(
            convert_raw<uint32_t>(FirmwareFeature::AICLK_ARB_MIN_ARBITER, min_raw.value()), AiclkMinArbiter::BUSY);
    }
    if (is_feature_available(FirmwareFeature::AICLK_ARB_MAX_ARBITER)) {
        arbitration.max_arbiter = to_enum(
            convert_raw<uint32_t>(FirmwareFeature::AICLK_ARB_MAX_ARBITER, max_raw.value()), AiclkMaxArbiter::HOST_FMAX);
    }

    const std::optional<uint32_t> target_raw = read_raw(FirmwareFeature::AICLK_TARGET_REASON);
    if (target_raw.has_value()) {
        const uint32_t reason_raw =
            convert_raw<uint32_t>(FirmwareFeature::AICLK_TARGET_REASON, target_raw.value()).value();
        const AiclkTargetReason reason = enum_from_raw(reason_raw, AiclkTargetReason::FORCED);
        arbitration.target_reason = reason;

        // Only the minimum and maximum arbiter reasons are caused by an arbiter. For the others, and for a reason UMD
        // does not recognize, the arbiter field carries no meaning.
        const std::optional<uint32_t> target_arbiter_raw =
            convert_raw<uint32_t>(FirmwareFeature::AICLK_TARGET_ARBITER, target_raw.value());
        if (target_arbiter_raw.has_value()) {
            if (reason == AiclkTargetReason::MIN_ARBITER) {
                arbitration.target_arbiter.emplace(
                    std::in_place_type<AiclkMinArbiter>,
                    enum_from_raw(target_arbiter_raw.value(), AiclkMinArbiter::BUSY));
            } else if (reason == AiclkTargetReason::MAX_ARBITER) {
                arbitration.target_arbiter.emplace(
                    std::in_place_type<AiclkMaxArbiter>,
                    enum_from_raw(target_arbiter_raw.value(), AiclkMaxArbiter::HOST_FMAX));
            }
        }
    }

    return arbitration;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_host_aiclk_limit(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::HOST_AICLK_LIMIT);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_smbus_error_count(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::SMBUS_ERRORS);
}

// The firmware's bit layout of the capability and active-config words stays in the feature maps; the public API only
// deals in FirmwareCapability values.
static std::optional<FirmwareFeature> capability_supported_feature(FirmwareCapability capability) {
    switch (capability) {
        case FirmwareCapability::KERNEL_NOP_THROTTLER:
            return FirmwareFeature::KERNEL_NOP_THROTTLER_SUPPORTED;
        case FirmwareCapability::GDDR_THERMAL_TRIP_ACTION:
            return FirmwareFeature::GDDR_THERMAL_TRIP_ACTION_SUPPORTED;
    }
    return std::nullopt;
}

static std::optional<FirmwareFeature> capability_enabled_feature(FirmwareCapability capability) {
    switch (capability) {
        case FirmwareCapability::KERNEL_NOP_THROTTLER:
            return FirmwareFeature::KERNEL_NOP_THROTTLER_ENABLED;
        case FirmwareCapability::GDDR_THERMAL_TRIP_ACTION:
            return FirmwareFeature::GDDR_THERMAL_TRIP_ACTION_ENABLED;
    }
    return std::nullopt;
}

std::optional<bool> FirmwareInfoProviderImplementation::is_capability_supported(
    FirmwareCapability capability, NocId noc_id) const {
    const std::optional<FirmwareFeature> feature = capability_supported_feature(capability);
    return feature.has_value() ? read_scalar<bool>(feature.value()) : std::nullopt;
}

std::optional<bool> FirmwareInfoProviderImplementation::is_capability_enabled(
    FirmwareCapability capability, NocId noc_id) const {
    const std::optional<FirmwareFeature> feature = capability_enabled_feature(capability);
    return feature.has_value() ? read_scalar<bool>(feature.value()) : std::nullopt;
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_kernel_nop_throttler_stop_freq(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::KERNEL_NOP_THROTTLER_STOP_FREQ);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_nop_start_count(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::NOP_START_COUNT);
}

std::optional<uint32_t> FirmwareInfoProviderImplementation::get_nop_on_duration(NocId noc_id) const {
    return read_scalar<uint32_t>(FirmwareFeature::NOP_ON_DURATION);
}

}  // namespace tt::umd
