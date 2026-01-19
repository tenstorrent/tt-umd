// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tt_access.h
 * @brief Tenstorrent Hardware Access API
 *
 * This header defines a stateless, mechanism-only C API for accessing
 * Tenstorrent AI accelerator hardware. It is designed to be consumed by
 * higher-level libraries.
 *
 * =============================================================================
 * DESIGN
 * =============================================================================
 *
 * - MECHANIC
 *   This API provides raw access to hardware. It does not decide when or why
 *   to perform operations - that is the responsibility of higher-level APIs.
 *
 * - STATELESS
 *   No opaque handles or implicit state. All operations take explicit device
 *   IDs and transport specifications. No open/close lifecycle to manage.
 *
 * - MODULAR
 *   The chip is modeled as a composition of IP blocks rather than a monolithic
 *   device abstraction.
 *
 * =============================================================================
 * TABLE OF CONTENTS
 * =============================================================================
 *
 * VERSION
 *  - Static and runtime API version query.
 *
 * ERRORS
 *  - Comprehensive error codes by category.
 *
 * DEVICE
 *  - Device identifier and discovery.
 *  - Architecture type enumeration.
 *
 * MESSAGING
 *  - Send commands to ARC controller.
 *
 * TELEMETRY
 *   - Device monitoring data.
 *
 * RESET
 *   - Device reset sequences.
 *
 * =============================================================================
 *
 * @version 0.1.0
 * @copyright Copyright (c) 2025 Tenstorrent Inc.
 */

#ifndef TT_ACCESS_H
#define TT_ACCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*============================================================================
 * VERSION
 *============================================================================*/

#define TT_VERSION_MAJOR 0
#define TT_VERSION_MINOR 1
#define TT_VERSION_PATCH 0
#define TT_VERSION \
    "v" #TT_VERSION_MAJOR "." #TT_VERSION_MINOR "." #TT_VERSION_PATCH

/**
 * @brief Get the API version at runtime.
 *
 * @param[out] major  Major version number
 * @param[out] minor  Minor version number
 * @param[out] patch  Patch version number
 *
 * Use this to verify ABI compatibility at runtime.
 */
void tt_version(int *major, int *minor, int *patch);

/*============================================================================
 * ERRORS
 *============================================================================*/

/**
 * @brief Error codes returned by API functions.
 *
 * All functions return TT_OK (0) on success, or a negative error code on
 * failure. Positive values are reserved for function-specific success codes.
 *
 * Error code ranges:
 *   -1   to  -99: General
 *   -100 to -199: Device
 *   -200 to -299: Transport
 *   -300 to -399: Hardware
 *   -400 to -499: Driver
 *   -500 to -599: Flash
 *   -600 to -699: Remote
 */
typedef enum tt_result {
    TT_OK                       =  0,   /**< Success */

    /* General errors (-1 to -99) */
    TT_ERROR_UNKNOWN            = -1,   /**< Unknown or unspecified error */
    TT_ERROR_INVALID_PARAM      = -2,   /**< Invalid parameter passed */
    TT_ERROR_NULL_POINTER       = -3,   /**< Unexpected NULL pointer */
    TT_ERROR_OUT_OF_MEMORY      = -4,   /**< Memory allocation failed */
    TT_ERROR_NOT_SUPPORTED      = -5,   /**< Operation not supported */
    TT_ERROR_BUFFER_TOO_SMALL   = -6,   /**< Provided buffer too small */
    TT_ERROR_INVALID_ADDRESS    = -7,   /**< Invalid address for operation */
    TT_ERROR_ALIGNMENT          = -8,   /**< Address/size alignment error */

    /* Device errors (-100 to -199) */
    TT_ERROR_DEV_NOT_FOUND      = -100, /**< Device not found */
    TT_ERROR_DEV_BUSY           = -101, /**< Device is busy */
    TT_ERROR_DEV_LOST           = -102, /**< Device removed or unavailable */
    TT_ERROR_DEV_HUNG           = -103, /**< Device is unresponsive */
    TT_ERROR_BAD_ARCH           = -104, /**< Invalid operation on architecture */

    /* Transport errors (-200 to -299) */
    TT_ERROR_TIMEOUT            = -200, /**< Operation timed out */
    TT_ERROR_ARC_MSG            = -205, /**< ARC message failed */

    /* Hardware state errors (-300 to -399) */
    TT_ERROR_ARC_NOT_READY      = -300, /**< ARC firmware not ready */
} tt_result_t;

/**
 * @brief Global error indicator.
 *
 * Set by API functions on failure. Check this for detailed error information
 * when a function returns an error. Similar to the C standard library's errno.
 *
 * This is thread-local storage.
 */
extern _Thread_local tt_result_t tt_errno;

/**
 * @brief Get a human-readable error message.
 *
 * @param result  The error code.
 * @return        Static string describing the error. NULL on `TT_OK`.
 */
const char *tt_result_describe(tt_result_t result);

/*============================================================================
 * DEVICE
 *============================================================================*/

/**
 * @brief Device architecture.
 */
typedef enum tt_arch {
    TT_ARCH_UNKNOWN    = 0,  /**< Unknown architecture */
    TT_ARCH_GRAYSKULL
        [[deprecated]] = 1,  /**< Grayskull */
    TT_ARCH_WORMHOLE   = 2,  /**< Wormhole */
    TT_ARCH_BLACKHOLE  = 3,  /**< Blackhole */
} tt_arch_t;

/**
 * @brief Get architecture name as string.
 */
const char *tt_arch_describe(tt_arch_t arch);

/**
 * @brief Device identifier.
 *
 * An integer identifying a device, similar to a file descriptor. Obtained from
 * enumeration.
 */
typedef uint32_t tt_device_t;

/** Invalid device ID sentinel value. */
#define TT_DEVICE_INVALID ((tt_device_t)-1)

/**
 * @brief Discover connected devices.
 *
 * Returns identifiers for all devices found.
 *
 * @param[out] devs Array to receive descriptors, or NULL to only count devices.
 * @param length    Maximum number of entries in array.
 * @return          Number of devices found (may exceed length), negative on error.
 *
 * Example:
 * @code
 *     int count = tt_device_discover(NULL, 0);
 *     if (count < 0)
 *       return count;
 *
 *     tt_device_t *devs = malloc(count * sizeof(*devs));
 *     tt_device_discover(devs, count);
 *
 *     for (int i = 0; i < count; i++) {
 *         printf(
 *             "Device %u: %s (%s)\n",
 *             devs[i].id,
 *             devs[i].board_name,
 *             tt_arch_describe(devs[i].arch)
 *         );
 *     }
 * @endcode
 */
int tt_device_discover(tt_device_t *devs, size_t length);

/**
 * @brief Device information.
 *
 * Contains a snapshot containing static information about a device.
 */
typedef struct tt_device_info {
    tt_device_t id;             /**< Device ID for use with API functions */
    tt_arch_t arch;             /**< Device architecture */
} tt_device_info_t;

/**
 * @brief Get information about a device.
 *
 * @param dev       Device identifier.
 * @param[out] info Receives device descriptor.
 * @return          TT_OK or error code.
 */
tt_result_t tt_get_device_info(tt_device_t dev, tt_device_info_t *info);

/*============================================================================
 * MESSAGING
 *
 * ARC is the embedded controller managing firmware, power, and clocks.
 *============================================================================*/

/**
 * @brief ARC message.
 */
typedef struct tt_arc_msg {
    uint8_t code;      /**< Message code */
    uint32_t data[8];  /**< Message data */
} tt_arc_msg_t;

/**
 * @brief Send a message to ARC.
 *
 * @param dev         Device identifier.
 * @param msg[in,out] ARC message body
 * @param wait        Wait for completion
 * @param timeout     Timeout (0 = default 1000ms)
 * @return            TT_OK or error code
 */
tt_result_t tt_arc_msg(
    tt_device_t dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
);

/*============================================================================
 * TELEMETRY
 *
 * Raw telemetry data read from device.
 *============================================================================*/

/**
 * @brief Telemetry tags.
 */
typedef enum tt_telemetry_tag {
    /** @brief High part of the board ID. */
    TT_TAG_BOARD_ID_HIGH        = 1,
    /** @brief Low part of the board ID. */
    TT_TAG_BOARD_ID_LOW         = 2,
    /** @brief ASIC ID. */
    TT_TAG_ASIC_ID              = 3,
    /** @brief Harvesting state of the system. */
    TT_TAG_HARVESTING_STATE     = 4,
    /** @brief Update interval for telemetry in milliseconds. */
    TT_TAG_UPDATE_TELEM_SPEED   = 5,
    /** @brief VCore voltage in millivolts. */
    TT_TAG_VCORE                = 6,
    /** @brief Thermal design power (TDP) in watts. */
    TT_TAG_TDP                  = 7,
    /** @brief Thermal design current (TDC) in amperes. */
    TT_TAG_TDC                  = 8,
    /** @brief VDD limits (min and max) in millivolts. */
    TT_TAG_VDD_LIMITS           = 9,
    /** @brief Thermal shutdown limit in degrees Celsius. */
    TT_TAG_THM_LIMIT_SHUTDOWN   = 10,
    /** @brief ASIC temperature in signed 16.16 fixed-point format. */
    TT_TAG_ASIC_TEMPERATURE     = 11,
    /** @brief Voltage regulator temperature in degrees Celsius. (Not implemented) */
    TT_TAG_VREG_TEMPERATURE     = 12,
    /** @brief Board temperature in degrees Celsius. (Not implemented) */
    TT_TAG_BOARD_TEMPERATURE    = 13,
    /** @brief AI clock frequency in megahertz. */
    TT_TAG_AICLK                = 14,
    /** @brief AXI clock frequency in megahertz. */
    TT_TAG_AXICLK               = 15,
    /** @brief ARC clock frequency in megahertz. */
    TT_TAG_ARCCLK               = 16,
    /** @brief L2CPU clock 0 frequency in megahertz. */
    TT_TAG_L2CPUCLK0            = 17,
    /** @brief L2CPU clock 1 frequency in megahertz. */
    TT_TAG_L2CPUCLK1            = 18,
    /** @brief L2CPU clock 2 frequency in megahertz. */
    TT_TAG_L2CPUCLK2            = 19,
    /** @brief L2CPU clock 3 frequency in megahertz. */
    TT_TAG_L2CPUCLK3            = 20,
    /** @brief Ethernet live status. */
    TT_TAG_ETH_LIVE_STATUS      = 21,
    /** @brief GDDR status. */
    TT_TAG_GDDR_STATUS          = 22,
    /** @brief GDDR speed in megabits per second. */
    TT_TAG_GDDR_SPEED           = 23,
    /** @brief Ethernet firmware version. */
    TT_TAG_ETH_FW_VERSION       = 24,
    /** @brief GDDR firmware version. */
    TT_TAG_GDDR_FW_VERSION      = 25,
    /** @brief DM application firmware version. */
    TT_TAG_DM_APP_FW_VERSION    = 26,
    /** @brief DM bootloader firmware version. */
    TT_TAG_DM_BL_FW_VERSION     = 27,
    /** @brief Flash bundle version. */
    TT_TAG_FLASH_BUNDLE_VERSION = 28,
    /** @brief CM firmware version. */
    TT_TAG_CM_FW_VERSION        = 29,
    /** @brief L2CPU firmware version. */
    TT_TAG_L2CPU_FW_VERSION     = 30,
    /** @brief Fan speed as a percentage. */
    TT_TAG_FAN_SPEED            = 31,
    /** @brief Timer heartbeat counter. */
    TT_TAG_TIMER_HEARTBEAT      = 32,
    /** @brief Total number of telemetry tags. */
    TT_TAG_TELEM_ENUM_COUNT     = 33,
    /** @brief Enabled Tensix columns. */
    TT_TAG_ENABLED_TENSIX_COL   = 34,
    /** @brief Enabled Ethernet interfaces. */
    TT_TAG_ENABLED_ETH          = 35,
    /** @brief Enabled GDDR interfaces. */
    TT_TAG_ENABLED_GDDR         = 36,
    /** @brief Enabled L2CPU cores. */
    TT_TAG_ENABLED_L2CPU        = 37,
    /** @brief PCIe usage information. */
    TT_TAG_PCIE_USAGE           = 38,
    /** @brief Input current in amperes. */
    TT_TAG_INPUT_CURRENT        = 39,
    /** @brief NOC translation status. */
    TT_TAG_NOC_TRANSLATION      = 40,
    /** @brief Fan RPM. */
    TT_TAG_FAN_RPM              = 41,
    /** @brief GDDR 0 and 1 temperature. */
    TT_TAG_GDDR_0_1_TEMP        = 42,
    /** @brief GDDR 2 and 3 temperature. */
    TT_TAG_GDDR_2_3_TEMP        = 43,
    /** @brief GDDR 4 and 5 temperature. */
    TT_TAG_GDDR_4_5_TEMP        = 44,
    /** @brief GDDR 6 and 7 temperature. */
    TT_TAG_GDDR_6_7_TEMP        = 45,
    /** @brief GDDR 0 and 1 corrected errors. */
    TT_TAG_GDDR_0_1_CORR_ERRS   = 46,
    /** @brief GDDR 2 and 3 corrected errors. */
    TT_TAG_GDDR_2_3_CORR_ERRS   = 47,
    /** @brief GDDR 4 and 5 corrected errors. */
    TT_TAG_GDDR_4_5_CORR_ERRS   = 48,
    /** @brief GDDR 6 and 7 corrected errors. */
    TT_TAG_GDDR_6_7_CORR_ERRS   = 49,
    /** @brief GDDR uncorrected errors. */
    TT_TAG_GDDR_UNCORR_ERRS     = 50,
    /** @brief Maximum GDDR temperature. */
    TT_TAG_MAX_GDDR_TEMP        = 51,
    /** @brief ASIC location. */
    TT_TAG_ASIC_LOCATION        = 52,
    /** @brief Board power limit in watts. */
    TT_TAG_BOARD_POWER_LIMIT    = 53,
    /** @brief Input power in watts. */
    TT_TAG_INPUT_POWER          = 54,
    /** @brief Maximum TDC limit in amperes. */
    TT_TAG_TDC_LIMIT_MAX        = 55,
    /** @brief Thermal throttle limit in degrees Celsius. */
    TT_TAG_THM_LIMIT_THROTTLE   = 56,
    /** @brief Firmware build date. */
    TT_TAG_FW_BUILD_DATE        = 57,
    /** @brief TT flash version. */
    TT_TAG_TT_FLASH_VERSION     = 58,
    /** @brief Enabled Tensix rows. */
    TT_TAG_ENABLED_TENSIX_ROW   = 59,
    /** @brief Thermal trip count. */
    TT_TAG_THERM_TRIP_COUNT     = 60,
    /** @brief High part of the ASIC ID. */
    TT_TAG_ASIC_ID_HIGH         = 61,
    /** @brief Low part of the ASIC ID. */
    TT_TAG_ASIC_ID_LOW          = 62,
    /** @brief Maximum AI clock frequency. */
    TT_TAG_AICLK_LIMIT_MAX      = 63,
    /** @brief Maximum TDP limit in watts. */
    TT_TAG_TDP_LIMIT_MAX        = 64,
    /**
    * @brief Effective minimum AICLK arbiter value in megahertz.
    *
    * This represents the highest frequency requested by all enabled minimum arbiters.
    * Multiple arbiters may request minimum frequencies, and the highest value is effective.
    *
    * @see @ref aiclk_arb_min
    */
    TT_TAG_AICLK_ARB_MIN        = 65,
    /**
    * @brief Effective maximum AICLK arbiter value in megahertz.
    *
    * This represents the lowest frequency limit imposed by all enabled maximum arbiters.
    * Multiple arbiters may impose maximum frequency limits (e.g., TDP, TDC, thermal throttling),
    * and the lowest (most restrictive) value is effective. This value takes precedence over
    * TT_TAG_AICLK_ARB_MIN when determining the final target frequency.
    *
    * @see @ref aiclk_arb_max
    */
    TT_TAG_AICLK_ARB_MAX        = 66,
    /**
    * @brief Bitmask of enabled minimum arbiters.
    *
    * Each bit represents whether a specific minimum frequency arbiter is currently enabled.
    * Bit positions correspond to the values in @ref aiclk_arb_min.
    *
    * @see @ref aiclk_arb_min
    */
    TT_TAG_ENABLED_MIN_ARB      = 67,
    /**
    * @brief Bitmask of enabled maximum arbiters.
    *
    * Each bit represents whether a specific maximum frequency arbiter is currently enabled.
    * Bit positions correspond to the values in @ref aiclk_arb_max.
    *
    * @see @ref aiclk_arb_max
    */
    TT_TAG_ENABLED_MAX_ARB      = 68,
    // Not a real telemetry tag.
    //
    // Used to determine the number of tags in the table.
    _TT_TELEMETRY_LENGTH [[deprecated("unstable: for internal use only")]],
} tt_telemetry_tag_t;

/**
 * @brief Telemetry data.
 */
typedef struct tt_telemetry {
    uint32_t table[_TT_TELEMETRY_LENGTH];
} tt_telemetry_t;

/**
 * @brief Read telemetry from device.
 *
 * @param dev            Device ID
 * @param[out] telemetry Receives telemetry data
 * @return               TT_OK or error code
 */
tt_result_t tt_get_telemetry(tt_device_t dev, tt_telemetry_t *telemetry);

#ifdef __cplusplus
}
#endif

#endif /* TT_ACCESS_H */
