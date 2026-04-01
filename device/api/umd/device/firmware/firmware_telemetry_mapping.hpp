// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <unordered_map>
#include <variant>

#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

// Standard telemetry tag (modern firmware, used in base class).
using StandardTag = TelemetryTag;

// Legacy Wormhole telemetry tag (firmware < 18.4.0).
using WormholeTag = wormhole::LegacyTelemetryTag;

// SMBus telemetry tag (for legacy Wormhole max_clock_freq).
struct SmBusTag {
    uint8_t tag;
};

// Fixed constant value (used when telemetry doesn't provide the data).
// Examples:
//   - Legacy WH ASIC_LOCATION: hardcoded to 0 (not available in telemetry)
//   - Legacy BH MAX_CLOCK_FREQ: hardcoded to AICLK_BUSY_VAL
//   - With NotAvailable{} converter: placeholder for features that don't exist
struct FixedValue {
    uint32_t value;
};

// FeatureKey: The "Where" - can be a standard enum, legacy enum, SMBus tag, or fixed value.
// Using a variant allows a single FirmwareFeatures map to hold keys of different types,
// so all features can be iterated and processed uniformly via std::visit.
using FeatureKey = std::variant<StandardTag, WormholeTag, SmBusTag, FixedValue>;

// Signedness of the raw telemetry value before scaling.
enum class NumericSign { UNSIGNED, SIGNED };

/**
 * LinearTransform: Applies shift, mask, scale, and offset to raw telemetry data.
 *
 * Formula: result = ((raw >> shift) & mask) * scale + offset
 *
 * When signedness is SIGNED, the masked value is reinterpreted as signed (int32_t)
 * before scaling. This is needed for s16.16 fixed-point formats (e.g. temperatures).
 *
 * Default values provide identity transform (pass-through).
 *
 * Examples:
 *   - Identity (pass-through): LinearTransform{} or LinearTransform{0, 0xFFFFFFFF, 1.0, 0.0}
 *   - ASIC temperature (modern, s16.16): LinearTransform{0, 0xFFFFFFFF, 1.0/65536.0, 0.0, NumericSign::SIGNED}
 *   - ASIC temperature (legacy WH): LinearTransform{0, 0xFFFF, 1.0/16.0, 0.0}
 *   - Max clock freq from AICLK: LinearTransform{16, 0xFFFF, 1.0, 0.0}
 *   - AICLK (legacy WH): LinearTransform{0, 0xFFFF, 1.0, 0.0}
 */
struct LinearTransform {
    uint32_t shift = 0;
    uint32_t mask = 0xFFFFFFFF;
    double scale = 1.0;
    double offset = 0.0;
    NumericSign signedness = NumericSign::UNSIGNED;
};

/**
 * NotAvailable: Feature is not available for this firmware/architecture combination.
 */
struct NotAvailable {};

// DataConverter: The "How" - can be linear math (including identity) or not available.
using DataConverter = std::variant<LinearTransform, NotAvailable>;

enum class FirmwareFeature {
    // Version information.
    FIRMWARE_VERSION,
    ETH_FW_VERSION,
    GDDR_FW_VERSION,
    CM_FW_VERSION,
    DM_APP_FW_VERSION,
    DM_BL_FW_VERSION,
    TT_FLASH_VERSION,

    // Board identification.
    BOARD_ID_HIGH,
    BOARD_ID_LOW,
    ASIC_LOCATION,

    // Temperature readings.
    ASIC_TEMPERATURE,
    BOARD_TEMPERATURE,
    GDDR_0_1_TEMP,
    GDDR_2_3_TEMP,
    GDDR_4_5_TEMP,
    GDDR_6_7_TEMP,
    MAX_GDDR_TEMP,

    // Clock frequencies and speed.
    AICLK,
    AXICLK,
    ARCCLK,
    MAX_CLOCK_FREQ,
    DDR_SPEED,

    // Power & voltage metrics.
    TDP,
    TDC,
    VCORE,
    TDC_LIMIT_MAX,
    BOARD_POWER_LIMIT,

    // Cooling & thermal management.
    FAN_SPEED,
    FAN_RPM,
    THM_LIMIT_THROTTLE,
    THM_LIMIT_SHUTDOWN,

    // General status.
    DDR_STATUS,
    HEARTBEAT,

    // RAS (Reliability) & error counters.
    ETH_LIVE_STATUS,
    THERM_TRIP_COUNT,
    GDDR_UNCORR_ERRS,
    GDDR_0_1_CORR_ERRS,
    GDDR_2_3_CORR_ERRS,
    GDDR_4_5_CORR_ERRS,
    GDDR_6_7_CORR_ERRS
};

/**
 * FeatureProfile: Decouples data retrieval (key) from data processing (converter).
 *
 * Stage 1 (Key): Where/how to fetch raw data
 * Stage 2 (Converter): How to transform raw data into the final result
 */
struct FeatureProfile {
    FeatureKey key;
    DataConverter converter;
};

// The configuration map: FirmwareFeature -> FeatureProfile.
using FirmwareFeatures = std::unordered_map<FirmwareFeature, FeatureProfile>;

}  // namespace tt::umd
