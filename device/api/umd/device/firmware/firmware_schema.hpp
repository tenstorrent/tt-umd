// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <variant>

#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace tt::umd {

// Standard telemetry tag (modern firmware, used in base class).
using StandardTag = TelemetryTag;

// Legacy Wormhole telemetry tag (firmware < 18.4.0).
using WormholeTag = wormhole::TelemetryTag;

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

// TelemetryKey: The "Where" - can be a standard enum, legacy enum, SMBus tag, or fixed value.
using TelemetryKey = std::variant<StandardTag, WormholeTag, SmBusTag, FixedValue>;

/**
 * LinearTransform: Applies shift, mask, scale, and offset to raw telemetry data.
 *
 * Formula: result = ((raw >> shift) & mask) * scale + offset
 *
 * Default values provide identity transform (pass-through).
 *
 * Examples:
 *   - Identity (pass-through): LinearTransform{} or LinearTransform{0, 0xFFFFFFFF, 1.0, 0.0}
 *   - ASIC temperature (modern): LinearTransform{0, 0xFFFFFFFF, 1.0/65536.0, 0.0}
 *   - ASIC temperature (legacy WH): LinearTransform{0, 0xFFFF, 1.0/16.0, 0.0}
 *   - Max clock freq from AICLK: LinearTransform{16, 0xFFFF, 1.0, 0.0}
 *   - AICLK (legacy WH): LinearTransform{0, 0xFFFF, 1.0, 0.0}
 */
struct LinearTransform {
    uint32_t shift = 0;
    uint32_t mask = 0xFFFFFFFF;
    double scale = 1.0;
    double offset = 0.0;
};

/**
 * NotAvailable: Feature is not available for this firmware/architecture combination.
 */
struct NotAvailable {};

// DataConverter: The "How" - can be linear math (including identity) or not available.
using DataConverter = std::variant<LinearTransform, NotAvailable>;

enum class TelemetryFeature {
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

    // Temperature readings.
    ASIC_TEMPERATURE,
    BOARD_TEMPERATURE,

    // Clock frequencies.
    AICLK,
    AXICLK,
    ARCCLK,
    MAX_CLOCK_FREQ,

    // Power metrics.
    FAN_SPEED,
    TDP,
    TDC,
    VCORE,

    // Status information.
    DDR_STATUS,
    ASIC_LOCATION,
    HEARTBEAT,
};

/**
 * FeatureProfile: Decouples data retrieval (key) from data processing (converter).
 *
 * Stage 1 (Key): Where/how to fetch raw data
 * Stage 2 (Converter): How to transform raw data into the final result
 */
struct FeatureProfile {
    TelemetryKey key;
    DataConverter converter;
};

// The configuration map: TelemetryFeature -> FeatureProfile.
using TelemetryFeatureMap = std::map<TelemetryFeature, FeatureProfile>;

}  // namespace tt::umd
