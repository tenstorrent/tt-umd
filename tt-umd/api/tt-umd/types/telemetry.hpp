// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

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
    THM_LIMIT_SHUTDOWN = 10,
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
    GDDR_STATUS = 22,
    GDDR_SPEED = 23,
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
    INPUT_CURRENT = 39,
    NOC_TRANSLATION = 40,
    FAN_RPM = 41,
    GDDR_0_1_TEMP = 42,
    GDDR_2_3_TEMP = 43,
    GDDR_4_5_TEMP = 44,
    GDDR_6_7_TEMP = 45,
    GDDR_0_1_CORR_ERRS = 46,
    GDDR_2_3_CORR_ERRS = 47,
    GDDR_4_5_CORR_ERRS = 48,
    GDDR_6_7_CORR_ERRS = 49,
    GDDR_UNCORR_ERRS = 50,
    MAX_GDDR_TEMP = 51,
    ASIC_LOCATION = 52,
    BOARD_POWER_LIMIT = 53,
    INPUT_POWER = 54,
    TDC_LIMIT_MAX = 55,
    THM_LIMIT_THROTTLE = 56,
    TT_FLASH_VERSION = 58,
    ENABLED_TENSIX_ROW = 59,
    THERM_TRIP_COUNT = 60,
    ASIC_ID_HIGH = 61,
    ASIC_ID_LOW = 62,
    AICLK_LIMIT_MAX = 63,
    TDP_LIMIT_MAX = 64,
    AICLK_ARB_MIN = 65,
    AICLK_ARB_MAX = 66,
    ENABLED_MIN_ARB = 67,
    ENABLED_MAX_ARB = 68,
    AICLK_PPM_INFO = 69,
    HOST_AICLK_LIMIT = 70,
    SMBUS_ERRORS = 71,
    GDDR_WEST_IO_POWER = 73,
    GDDR_EAST_IO_POWER = 74,
    KERNEL_THROTTLER = 75,
    NOP_START_COUNT = 76,
    NOP_ON_DURATION = 77,
    // Tags 78 and 79 are Blackhole-only names. Wormhole firmware uses the same numbers for its second fan's
    // speed and RPM, so these must only be read through Blackhole feature maps.
    FW_CAPABILITIES_0 = 78,
    FW_ACTIVE_CONFIG_0 = 79,
    NUMBER_OF_TAGS = 80,
};

// Operating mode of a PCIe instance, as configured in the board's firmware table.
// UNKNOWN is reported for a mode value this version of UMD does not recognize. It is the maximum value of the
// underlying type so it can never collide with a mode that firmware defines.
enum class PcieMode : uint8_t {
    DISABLED = 0,
    ENDPOINT = 1,
    ROOT_PORT = 2,
    UNKNOWN = 0xFF,
};

// AICLK maximum-frequency arbiters. Each enabled arbiter imposes a ceiling on AICLK and the lowest ceiling wins.
// Firmware keeps these values stable across releases; newer firmware may append arbiters past the last one listed,
// which UMD reports as UNKNOWN. UNKNOWN is the maximum value of the underlying type so it never collides with a real
// arbiter.
enum class AiclkMaxArbiter : uint16_t {
    FMAX = 0,              // Chip's configured maximum AICLK.
    TDP = 1,               // Thermal design power limit.
    FAST_TDC = 2,          // Fast-response thermal design current limit.
    TDC = 3,               // Thermal design current limit.
    THERMAL = 4,           // ASIC temperature throttle.
    BOARD_POWER = 5,       // Board power limit.
    VOLTAGE = 6,           // Highest AICLK the maximum allowed voltage supports.
    GDDR_THERMAL = 7,      // GDDR temperature throttle.
    DOPPLER_SLOW = 8,      // Slow-response di/dt (Doppler) throttle.
    DOPPLER_CRITICAL = 9,  // Critical di/dt (Doppler) throttle.
    HOST_FMAX = 10,        // Ceiling set by the host, see FirmwareInfoProvider::get_host_aiclk_limit().
    UNKNOWN = 0xFFFF,      // An arbiter this version of UMD does not recognize.
};

// AICLK minimum-frequency arbiters. Each enabled arbiter imposes a floor on AICLK and the highest floor wins.
// Firmware keeps these values stable across releases; newer firmware may append arbiters past the last one listed.
enum class AiclkMinArbiter : uint16_t {
    FMIN = 0,          // Chip's configured minimum AICLK.
    BUSY = 1,          // Floor requested while the host has the chip in the busy power state.
    UNKNOWN = 0xFFFF,  // An arbiter this version of UMD does not recognize.
};

// Why the firmware chose the current AICLK target.
enum class AiclkTargetReason : uint16_t {
    MIN_ARBITER = 0,   // A minimum arbiter set the target; the arbiter is an AiclkMinArbiter.
    MAX_ARBITER = 1,   // A maximum arbiter set the target; the arbiter is an AiclkMaxArbiter.
    FMIN = 2,          // Arbitration landed below the chip's minimum AICLK, so the minimum was used.
    SWEEP = 3,         // A characterization sweep is choosing random frequencies.
    FORCED = 4,        // AICLK is forced to a fixed value.
    UNKNOWN = 0xFFFF,  // A reason this version of UMD does not recognize.
};

// A consistent view of the Blackhole AICLK arbitration state, see FirmwareInfoProvider::get_aiclk_arbitration().
struct AiclkArbitration {
    // Effective floor: the highest frequency requested by any enabled minimum arbiter.
    uint32_t min_freq_mhz;
    // Effective ceiling: the lowest limit imposed by any enabled maximum arbiter. The ceiling takes precedence over
    // the floor when the two conflict.
    uint32_t max_freq_mhz;
    // Arbiters the firmware has enabled. Enabled arbiters this version of UMD does not recognize are reported as a
    // single UNKNOWN entry.
    std::vector<AiclkMinArbiter> enabled_min_arbiters;
    std::vector<AiclkMaxArbiter> enabled_max_arbiters;
    // The arbiters that set min_freq_mhz and max_freq_mhz, i.e. what is limiting AICLK. Published from firmware 19.7.
    std::optional<AiclkMinArbiter> min_arbiter;
    std::optional<AiclkMaxArbiter> max_arbiter;
    // Why the firmware chose the current AICLK target. Published from firmware 19.7.
    std::optional<AiclkTargetReason> target_reason;
    // The arbiter that set the current AICLK target. Only set when target_reason is MIN_ARBITER (holds an
    // AiclkMinArbiter) or MAX_ARBITER (holds an AiclkMaxArbiter); the other reasons are not set by an arbiter.
    std::optional<std::variant<AiclkMinArbiter, AiclkMaxArbiter>> target_arbiter;
};

// Optional firmware features that the firmware reports as supported and, separately, as enabled. Blackhole only. See
// FirmwareInfoProvider::is_capability_supported() and is_capability_enabled().
enum class FirmwareCapability {
    // Cuts power when lowering AICLK is not enough: once AICLK is at its minimum and core power is still above the TDP
    // limit, the firmware tells the Tensix cores to execute NOPs between kernel instructions. It stops once AICLK has
    // recovered to the stop frequency (see FirmwareInfoProvider::get_kernel_nop_throttler_stop_freq()) and power is
    // back under the limit. The host can toggle it at runtime.
    KERNEL_NOP_THROTTLER,
    // The firmware's action when a GDDR thermal trip occurs.
    GDDR_THERMAL_TRIP_ACTION,
};

}  // namespace tt::umd
