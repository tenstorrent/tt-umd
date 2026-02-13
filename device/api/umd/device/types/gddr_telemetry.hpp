// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

namespace tt::umd {

/** Number of GDDR modules (channels) per device. */
constexpr std::size_t NUM_GDDR_MODULES = 8U;

/**
 * Per-module GDDR telemetry for monitoring and early warning of DRAM issues.
 * Layout matches tt-zephyr-platforms bh_arc telemetry (telemetry.c).
 */
struct GddrModuleTelemetry {
    /** Temperature at top of module (C). 0 if not available. */
    uint8_t temperature_top{0};
    /** Temperature at bottom of module (C). 0 if not available. */
    uint8_t temperature_bottom{0};
    /** Corrected EDC read errors for this module. */
    uint8_t corrected_read_errors{0};
    /** Corrected EDC write errors for this module. */
    uint8_t corrected_write_errors{0};
    /** True if an uncorrected read EDC error has occurred on this module. */
    bool uncorrected_read_error{false};
    /** True if an uncorrected write EDC error has occurred on this module. */
    bool uncorrected_write_error{false};
    /** True if training completed successfully for this module. */
    bool training_complete{false};
    /** True if GDDR error reported for this module. */
    bool error{false};
};

/**
 * Aggregated GDDR telemetry for the device.
 * Useful for monitoring and detection/early warning of DRAM failure.
 */
struct GddrTelemetry {
    /** Per-module telemetry (indices 0-7). */
    std::array<GddrModuleTelemetry, NUM_GDDR_MODULES> modules{};
    /** Maximum temperature across all modules (C). */
    uint8_t max_temperature{0};
    /** GDDR speed in Mbps. */
    uint32_t speed_mbps{0};
    /**
     * Raw status word: [i*2] = training complete for module i,
     * [i*2+1] = error for module i (i=0..7).
     */
    uint32_t status{0};
    /**
     * Uncorrected errors bit mask: [i*2] = uncorrected read for module i,
     * [i*2+1] = uncorrected write for module i (i=0..7).
     */
    uint32_t uncorrected_errors_mask{0};
};

}  // namespace tt::umd
