// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once


namespace tt::umd::wormhole {

enum TelemetryTag : uint8_t {
    ENUM_VERSION = 0,
    DEVICE_ID = 1,
    ASIC_RO = 2,
    ASIC_IDD = 3,
    BOARD_ID_HIGH = 4,
    BOARD_ID_LOW = 5,
    ARC0_FW_VERSION = 6,
    ARC1_FW_VERSION = 7,
    ARC2_FW_VERSION = 8,
    ARC3_FW_VERSION = 9,
    SPIBOOTROM_FW_VERSION = 10,
    ETH_FW_VERSION = 11,
    DM_BL_FW_VERSION = 12,
    DM_APP_FW_VERSION = 13,
    DDR_STATUS = 14,
    ETH_STATUS0 = 15,
    ETH_STATUS1 = 16,
    PCIE_STATUS = 17,
    FAULTS = 18,
    ARC0_HEALTH = 19,
    ARC1_HEALTH = 20,
    ARC2_HEALTH = 21,
    ARC3_HEALTH = 22,
    FAN_SPEED = 23,
    AICLK = 24,
    AXICLK = 25,
    ARCCLK = 26,
    THROTTLER = 27,
    VCORE = 28,
    ASIC_TEMPERATURE = 29,
    VREG_TEMPERATURE = 30,
    BOARD_TEMPERATURE = 31,
    TDP = 32,
    TDC = 33,
    VDD_LIMITS = 34,
    THM_LIMITS = 35,
    WH_FW_DATE = 36,
    ASIC_TMON0 = 37,
    ASIC_TMON1 = 38,
    MVDDQ_POWER = 39,
    GDDR_TRAIN_TEMP0 = 40,
    GDDR_TRAIN_TEMP1 = 41,
    BOOT_DATE = 42,
    RT_SECONDS = 43,
    ETH_DEBUG_STATUS0 = 44,
    ETH_DEBUG_STATUS1 = 45,
    TT_FLASH_VERSION = 46,
    ETH_LOOPBACK_STATUS = 47,
    ETH_LIVE_STATUS = 48,
    FW_BUNDLE_VERSION = 49,
    NUMBER_OF_TAGS = 50
};

} // namespace tt::umd::wormhole

