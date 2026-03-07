// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <unordered_map>

#include "umd/device/types/arch.hpp"

namespace tt::umd {

enum class GddrModule {
    GDDR_0 = 0,
    GDDR_1 = 1,
    GDDR_2 = 2,
    GDDR_3 = 3,
    GDDR_4 = 4,
    GDDR_5 = 5,
    NUM_OF_WORMHOLE_MODULES = 6,
    GDDR_6 = 6,
    GDDR_7 = 7,
    NUM_OF_BLACKHOLE_MODULES = 8
};

inline size_t get_number_of_dram_modules(const ARCH arch) {
    switch (arch) {
        case ARCH::WORMHOLE_B0:
            return static_cast<size_t>(GddrModule::NUM_OF_WORMHOLE_MODULES);
        case ARCH::BLACKHOLE:
            return static_cast<size_t>(GddrModule::NUM_OF_BLACKHOLE_MODULES);
        default:
            throw std::runtime_error("Unsupported architecture for DRAM module count.");
    }
}

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
    std::unordered_map<GddrModule, GddrModuleTelemetry> modules;
};

}  // namespace tt::umd
