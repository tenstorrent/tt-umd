// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

enum TelemetryTag : uint8_t {
    BOARD_ID_HIGH = 1,
    BOARD_ID_LOW = 2,
    ASIC_ID = 3,
    HARVESTING_STATE = 4,
    UPDATE_TELEM_SPEED = 5,
    VCORE = 6,
    TDP = 7,
    TDC = 8,
    VDD_LIMITS = 9,
    THM_LIMITS = 10,
    ASIC_TEMPERATURE = 11,
    VREG_TEMPERATURE = 12,
    BOARD_TEMPERATURE = 13,
    AICLK = 14,
    AXICLK = 15,
    ARCCLK = 16,
    L2CPUCLK0 = 17,
    L2CPUCLK1 = 18,
    L2CPUCLK2 = 19,
    L2CPUCLK3 = 20,
    ETH_LIVE_STATUS = 21,
    DDR_STATUS = 22,
    DDR_SPEED = 23,
    ETH_FW_VERSION = 24,
    GDDR_FW_VERSION = 25,
    DM_APP_FW_VERSION = 26,
    DM_BL_FW_VERSION = 27,
    FLASH_BUNDLE_VERSION = 28,
    CM_FW_VERSION = 29,
    L2CPU_FW_VERSION = 30,
    FAN_SPEED = 31,
    TIMER_HEARTBEAT = 32,
    TELEMETRY_ENUM_COUNT = 33,
    ENABLED_TENSIX_COL = 34,
    ENABLED_ETH = 35,
    ENABLED_GDDR = 36,
    ENABLED_L2CPU = 37,
    PCIE_USAGE = 38,
    /** Packed: [31:24] GDDR1 top, [23:16] GDDR1 bottom, [15:8] GDDR0 top, [7:0] GDDR0 bottom (C) */
    GDDR_0_1_TEMP = 39,
    /** Packed: [31:24] GDDR3 top, [23:16] GDDR3 bottom, [15:8] GDDR2 top, [7:0] GDDR2 bottom (C) */
    GDDR_2_3_TEMP = 40,
    /** Packed: [31:24] GDDR5 top, [23:16] GDDR5 bottom, [15:8] GDDR4 top, [7:0] GDDR4 bottom (C) */
    GDDR_4_5_TEMP = 41,
    /** Packed: [31:24] GDDR7 top, [23:16] GDDR7 bottom, [15:8] GDDR6 top, [7:0] GDDR6 bottom (C) */
    GDDR_6_7_TEMP = 42,
    /** Packed: [31:24] GDDR1 corr write, [23:16] GDDR1 corr read, [15:8] GDDR0 corr write, [7:0] GDDR0 corr read */
    GDDR_0_1_CORR_ERRS = 43,
    GDDR_2_3_CORR_ERRS = 44,
    GDDR_4_5_CORR_ERRS = 45,
    GDDR_6_7_CORR_ERRS = 46,
    /** Bit mask: [i*2] = GDDR i uncorrected read, [i*2+1] = GDDR i uncorrected write (i=0..7) */
    GDDR_UNCORR_ERRS = 47,
    /** Maximum temperature across all GDDR modules (C) */
    MAX_GDDR_TEMP = 48,
    NOC_TRANSLATION = 40,
    FAN_RPM = 41,
    ASIC_LOCATION = 52,
    TDC_LIMIT_MAX = 55,
    TT_FLASH_VERSION = 58,
    ASIC_ID_HIGH = 61,
    ASIC_ID_LOW = 62,
    AICLK_LIMIT_MAX = 63,
    TDP_LIMIT_MAX = 64,
    NUMBER_OF_TAGS = 65,
};

}  // namespace tt::umd
