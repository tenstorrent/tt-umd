// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>

#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/types/wormhole_telemetry.hpp"

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
    TDP_LIMIT_MAX,
    BOARD_POWER_LIMIT,

    // Cooling & thermal management.
    FAN_SPEED,
    FAN_RPM,
    THM_LIMIT_THROTTLE,
    THM_LIMIT_SHUTDOWN,

    // General status.
    DDR_STATUS,
    HEARTBEAT,

    // Ethernet status (all read from the ETH_LIVE_STATUS telemetry tag).
    ETH_HEARTBEAT_STATUS,
    ETH_RETRAIN_STATUS,
    ETH_LINK_STATUS,

    // RAS (Reliability) & error counters.
    THERM_TRIP_COUNT,
    GDDR_UNCORR_ERRS,
    GDDR_0_1_CORR_ERRS,
    GDDR_2_3_CORR_ERRS,
    GDDR_4_5_CORR_ERRS,
    GDDR_6_7_CORR_ERRS,

    // Voltage limits.
    VDD_MAX,
    VDD_MIN,

    // L2CPU cluster clocks.
    L2CPUCLK0,
    L2CPUCLK1,
    L2CPUCLK2,
    L2CPUCLK3,

    // Enabled (non-harvested) units, as firmware bitmasks.
    ENABLED_TENSIX_COL,
    ENABLED_TENSIX_ROW,
    ENABLED_ETH,
    ENABLED_GDDR,
    ENABLED_L2CPU,
    PCIE_0_MODE,
    PCIE_1_MODE,

    // Board input and GDDR IO power.
    INPUT_POWER,
    GDDR_WEST_IO_POWER,
    GDDR_EAST_IO_POWER,

    // AICLK arbitration.
    AICLK_ARB_MIN_FREQ,
    AICLK_ARB_MIN_ARBITER,
    AICLK_ARB_MAX_FREQ,
    AICLK_ARB_MAX_ARBITER,
    ENABLED_AICLK_MIN_ARBITERS,
    ENABLED_AICLK_MAX_ARBITERS,
    AICLK_TARGET_ARBITER,
    AICLK_TARGET_REASON,
    HOST_AICLK_LIMIT,

    // SMBus.
    SMBUS_ERRORS,

    // Firmware capabilities, each as a supported and an enabled flag.
    KERNEL_NOP_THROTTLER_SUPPORTED,
    KERNEL_NOP_THROTTLER_ENABLED,
    GDDR_THERMAL_TRIP_ACTION_SUPPORTED,
    GDDR_THERMAL_TRIP_ACTION_ENABLED,

    // Kernel NOP throttler.
    KERNEL_NOP_THROTTLER_STOP_FREQ,
    NOP_START_COUNT,
    NOP_ON_DURATION,
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
    // When set, this feature is only available while the named feature is available and reads non-zero.
    // Used for values the firmware publishes only when a capability flag says the feature exists.
    std::optional<FirmwareFeature> gated_by = std::nullopt;
};

// The configuration map: FirmwareFeature -> FeatureProfile.
using FirmwareFeatures = std::unordered_map<FirmwareFeature, FeatureProfile>;

}  // namespace tt::umd
