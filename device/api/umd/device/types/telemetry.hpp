/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
    NUMBER_OF_TAGS = 39,
    ASIC_LOCATION = 52,
    TT_FLASH_VERSION = 58,
    ASIC_ID_HIGH = 61,
    ASIC_ID_LOW = 62,
    AICLK_LIMIT_MAX = 63,
};

}  // namespace tt::umd
