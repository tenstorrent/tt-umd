// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>

namespace tt::umd {

inline constexpr int NUM_GDDR_MODULES = 8;

// GDDR modules for Blackhole.
enum class BlackholeGddr {
    GDDR_0 = 0,
    GDDR_1 = 1,
    GDDR_2 = 2,
    GDDR_3 = 3,
    GDDR_4 = 4,
    GDDR_5 = 5,
    GDDR_6 = 6,
    GDDR_7 = 7
};

struct GddrModuleTelemetry {
    // Temperature in Celsius of the top DRAM die.
    uint16_t dram_temperature_top{0};
    // Temperature in Celsius of the bottom DRAM die.
    uint16_t dram_temperature_bottom{0};
    // Saturates at 255. Number of cumulative corrected EDC errors on read since reset.
    uint8_t corr_edc_rd_errors{0};
    // Saturates at 255. Number of cumulative corrected EDC errors on write since reset.
    uint8_t corr_edc_wr_errors{0};
    // 1 if any uncorrected EDC errors on read since reset.
    uint8_t uncorr_edc_rd_error{0};
    // 1 if any uncorrected EDC errors on write since reset.
    uint8_t uncorr_edc_wr_error{0};
};

struct GddrTelemetry {
    std::unordered_map<BlackholeGddr, GddrModuleTelemetry> modules;
};

}  // namespace tt::umd
