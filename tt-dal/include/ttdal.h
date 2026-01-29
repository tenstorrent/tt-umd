// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ttdal.h
 * @brief Tenstorrent Device Access Library.
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
 * TLB
 *  - Translation Lookaside Buffer allocations.
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

#ifndef TT_DAL_H
#define TT_DAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

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
 * @param[out] major  Major version number.
 * @param[out] minor  Minor version number.
 * @param[out] patch  Patch version number.
 *
 * Use this to verify ABI compatibility at runtime.
 */
static inline void tt_version(int *major, int *minor, int *patch) {
    if (major)
        *major = TT_VERSION_MAJOR;
    if (minor)
        *minor = TT_VERSION_MINOR;
    if (patch)
        *patch = TT_VERSION_PATCH;
}

/*============================================================================
 * ERRORS
 *============================================================================*/

/**
 * @brief Error codes returned by API functions.
 *
 * API functions return `TT_OK` (0) on success or a negative error code
 * on failure. The thread-local `tt_errno` variable stores the positive
 * error code, suitable for use as process exit codes.
 *
 * Example pattern:
 * @code
 *     return -(tt_errno = TT_ENODEV);
 * @endcode
 * This returns `-10` to the caller while setting `tt_errno` to `10`.
 *
 * Error codes follow errno conventions where possible. Codes are
 * organized by category within the 8-bit exit code range (0-255):
 *   1-9:   General
 *   10-19: Device
 *   20-29: Transport
 *   30-39: Hardware
 */
typedef enum tt_result {
    TT_OK                       = 0,    /**< Success. */

    /* General errors (1-9) */
    TT_EINVAL                   = 1,    /**< Invalid argument. */
    TT_ENOMEM                   = 2,    /**< Out of memory. */
    TT_ENOTSUP                  = 3,    /**< Operation not supported. */
    TT_ENOBUFS                  = 4,    /**< No buffer space available. */
    TT_EALIGN                   = 5,    /**< Alignment error. */
    TT_EIO                      = 6,    /**< I/O error. */

    /* Device errors (10-19) */
    TT_ENODEV                   = 10,   /**< No such device. */
    TT_EBUSY                    = 11,   /**< Device or resource busy. */
    TT_ENOTOPEN                 = 12,   /**< Device not open. */
    TT_EDEVLOST                 = 13,   /**< Device lost. */
    TT_EDEVHUNG                 = 14,   /**< Device hung. */
    TT_EBADARCH                 = 15,   /**< Unsupported architecture. */
    TT_EACCES                   = 16,   /**< Permission denied. */

    /* Transport errors (20-29) */
    TT_ETIMEDOUT                = 20,   /**< Operation timed out. */
    TT_EARCMSG                  = 21,   /**< ARC message failed. */

    /* Hardware state errors (30-39) */
    TT_ENOTREADY                = 30,   /**< Device not ready. */
} tt_result_t;

/**
 * @brief Global error indicator.
 *
 * Set by API functions on failure. Check this for detailed error
 * information when a function returns an error. Similar to the C
 * standard library's `errno`.
 *
 * This is thread-local storage.
 */
extern _Thread_local tt_result_t tt_errno;

/**
 * @brief Get a human-readable error message.
 *
 * @param result  The error code.
 * @return        Static string describing the error; `NULL` on `TT_OK`.
 */
static inline const char *tt_result_describe(tt_result_t result) {
    switch (result) {
        case TT_OK:
            return NULL;
        case TT_EINVAL:
            return "invalid argument";
        case TT_ENOMEM:
            return "out of memory";
        case TT_ENOTSUP:
            return "operation not supported";
        case TT_ENOBUFS:
            return "no buffer space available";
        case TT_EALIGN:
            return "alignment error";
        case TT_EIO:
            return "I/O error";
        case TT_ENODEV:
            return "no such device";
        case TT_EBUSY:
            return "device or resource busy";
        case TT_ENOTOPEN:
            return "device not open";
        case TT_EDEVLOST:
            return "device lost";
        case TT_EDEVHUNG:
            return "device hung";
        case TT_EBADARCH:
            return "unsupported architecture";
        case TT_EACCES:
            return "permission denied";
        case TT_ETIMEDOUT:
            return "operation timed out";
        case TT_EARCMSG:
            return "ARC message failed";
        case TT_ENOTREADY:
            return "device not ready";
        default:
            return "unknown error";
    }
}

/*============================================================================
 * DEVICE
 *============================================================================*/

/**
 * @brief Device architecture.
 *
 * Values are PCI device IDs.
 */
typedef enum tt_arch {
    TT_ARCH_UNKNOWN    = 0,
    TT_ARCH_GRAYSKULL
        [[deprecated]] = 0xffa0, /**< Grayskull. */
    TT_ARCH_WORMHOLE   = 0x401e, /**< Wormhole. */
    TT_ARCH_BLACKHOLE  = 0xb140, /**< Blackhole. */
} tt_arch_t;

/**
 * @brief Get architecture name as string.
 *
 * @param arch  The architecture type.
 * @return      Static string describing the architecture; `NULL` for
 *              unknown/invalid.
 */
static inline const char *tt_arch_describe(tt_arch_t arch) {
    switch (arch) {
        case TT_ARCH_GRAYSKULL:
            return "Grayskull";
        case TT_ARCH_WORMHOLE:
            return "Wormhole";
        case TT_ARCH_BLACKHOLE:
            return "Blackhole";
        case TT_ARCH_UNKNOWN:
        default:
            return NULL;
    }
}

/**
 * @brief Device handle.
 *
 * Contains device number and file descriptor. The fd is opened on
 * first use via `tt_device_open()` and remains open until
 * `tt_device_close()` is called.
 */
typedef struct tt_device {
    /**
     * @brief Device number.
     */
    uint32_t id;
    /**
     * @brief File descriptor (`-1` if not open).
     */
    int fd;
} tt_device_t;

/**
 * @brief Discover connected devices.
 *
 * Returns device handles with unopened file descriptors (`fd = -1`).
 * Call `tt_device_open()` before using a device. Fills buffer with
 * up to `cap` devices. Return value may exceed `cap` if more devices
 * exist.
 *
 * @param cap       Capacity of buffer.
 * @param[out] buf  Buffer to receive device handles.
 * @return          Number of devices found (may exceed `cap`),
 *                  negative on error.
 *
 * Example:
 * @code
 *     tt_device_t devs[16];
 *     ssize_t count = tt_device_discover(16, devs);
 *     if (count < 0)
 *       return count;
 *
 *     size_t actual = (count < 16) ? count : 16;
 *     for (size_t i = 0; i < actual; i++) {
 *         tt_device_open(&devs[i]);
 *         // ... use device ...
 *         tt_device_close(&devs[i]);
 *     }
 * @endcode
 */
ssize_t tt_device_discover(size_t cap, tt_device_t buf[static cap]);

/**
 * @brief Open device file descriptor.
 *
 * Opens the device fd if not already open. Multiple calls are safe
 * (NOP if `fd != -1`).
 *
 * @param dev   Device handle.
 * @return      `TT_OK` or error code.
 */
tt_result_t tt_device_open(tt_device_t *dev);

/**
 * @brief Close device file descriptor.
 *
 * Closes the device fd if open. Multiple calls are safe (NOP if
 * `fd == -1`).
 *
 * @param dev   Device handle.
 * @return      `TT_OK` or error code.
 */
tt_result_t tt_device_close(tt_device_t *dev);

/**
 * @brief Device information.
 *
 * Contains a snapshot of static information about a device.
 */
typedef struct tt_device_info {
    /**
     * @brief Size of output structure.
     */
    uint32_t output_size_bytes;
    /**
     * @brief PCI vendor ID.
     */
    uint16_t vendor_id;
    /**
     * @brief PCI device ID.
     */
    uint16_t device_id;
    /**
     * @brief PCI subsystem vendor ID.
     */
    uint16_t subsystem_vendor_id;
    /**
     * @brief PCI subsystem ID.
     */
    uint16_t subsystem_id;
    /**
     * @brief PCI BDF (bus/device/function).
     */
    uint16_t bus_dev_fn;
    /**
     * @brief Max DMA buffer size (log2).
     */
    uint16_t max_dma_buf_size_log2;
    /**
     * @brief PCI domain.
     */
    uint16_t pci_domain;
} tt_device_info_t;

/**
 * @brief Get information about a device.
 *
 * @param dev       Device handle.
 * @param[out] info Device information output.
 * @return          `TT_OK` or error code.
 */
tt_result_t tt_get_device_info(tt_device_t *dev, tt_device_info_t *info);

/*============================================================================
 * TLB ALLOCATIONS
 *
 * TLB (Translation Lookaside Buffer) windows provide direct
 * memory-mapped access to device NOC addresses. These are fixed-size
 * apertures that transparently translate host memory operations to
 * device transactions.
 *============================================================================*/

/**
 * @brief TLB size.
 *
 * Valid TLB window sizes. Not all sizes are available on all
 * architectures; kernel validates availability.
 */
typedef enum tt_tlb_size {
    /**
     * 1MB window.
     *
     * Supported: Wormhole
     */
    TT_TLB_1MB  = (1UL << 20),
    /**
     * 2MB window.
     *
     * Supported: Wormhole, Blackhole
     */
    TT_TLB_2MB  = (1UL << 21),
    /**
     * 16MB window.
     *
     * Supported: Wormhole
     */
    TT_TLB_16MB = (1UL << 24),
    /**
     * 4GB window.
     *
     * Supported: Blackhole
     */
    TT_TLB_4GB  = (1UL << 32),
} tt_tlb_size_t;

/**
 * @brief TLB handle.
 *
 * Contains TLB identifier, mapped pointer, and size. Allocated by
 * `tt_tlb_alloc()` and freed by `tt_tlb_free()`. The `ptr` field
 * is `NULL` until `tt_tlb_configure()` is called.
 */
typedef struct tt_tlb {
    /**
     * @brief TLB identifier.
     */
    uint32_t id;
    /**
     * @brief Memory-mapped window (`NULL` until configured).
     */
    void *ptr;
    /**
     * @brief Window size in bytes.
     */
    size_t len;
    /**
     * @brief mmap offset (for internal use).
     */
    uint64_t offset;
} tt_tlb_t;

/**
 * @brief TLB cache mode.
 */
typedef enum tt_tlb_cache_mode {
    TT_TLB_UC = 0,  /**< Uncached (for registers). */
    TT_TLB_WC = 1,  /**< Write-combined (for memory). */
} tt_tlb_cache_mode_t;

/**
 * @brief TLB ordering mode.
 */
typedef enum tt_tlb_ordering {
    TT_TLB_RELAXED = 0,  /**< Relaxed ordering. */
    TT_TLB_STRICT  = 1,  /**< Strict ordering (full AXI). */
    TT_TLB_POSTED  = 2,  /**< Posted writes. */
} tt_tlb_ordering_t;

/**
 * @brief TLB NOC configuration.
 *
 * Specifies the NOC target and address mapping for a TLB
 * window.
 */
typedef struct tt_tlb_config {
    /**
     * @brief Device address.
     */
    uint64_t addr;
    /**
     * @brief Target X coordinate.
     */
    uint8_t x_end;
    /**
     * @brief Target Y coordinate.
     */
    uint8_t y_end;
    /**
     * @brief Multicast start X.
     */
    uint8_t x_start;
    /**
     * @brief Multicast start Y.
     */
    uint8_t y_start;
    /**
     * @brief NOC selector (`0` or `1`).
     */
    uint8_t noc;
    /**
     * @brief Ordering mode.
     */
    tt_tlb_ordering_t ordering;
    /**
     * @brief Multicast enable.
     */
    bool mcast;
    /**
     * @brief Linked TLB flag.
     */
    bool linked;
    /**
     * @brief Static virtual channel.
     */
    uint8_t static_vc;
} tt_tlb_config_t;

/**
 * @brief Allocate a TLB window.
 *
 * Allocates a TLB of the requested size. The kernel validates size
 * availability for the device architecture. The returned TLB has
 * `ptr = NULL` and must be configured with `tt_tlb_configure()`
 * before use. Accessing `ptr` before configuration results in
 * `SIGSEGV`.
 *
 * @param dev      Device handle.
 * @param size     Window size.
 * @param mode     Cache mode.
 * @param[out] tlb Allocated TLB handle with `ptr = NULL`.
 * @return         `TT_OK` or error code.
 */
tt_result_t tt_tlb_alloc(
    tt_device_t *dev,
    tt_tlb_size_t size,
    tt_tlb_cache_mode_t mode,
    tt_tlb_t *tlb
);

/**
 * @brief Configure TLB NOC mapping.
 *
 * Sets the NOC target address and coordinates. On first call, maps
 * the TLB into the process address space and sets `tlb->ptr`.
 * Reconfiguring an already-mapped TLB will unmap and remap to
 * invalidate stale interior pointers (fail-fast on misuse).
 *
 * @param dev   Device handle.
 * @param tlb   TLB handle.
 * @param cfg   NOC configuration.
 * @return      `TT_OK` or error code.
 */
tt_result_t tt_tlb_configure(
    tt_device_t *dev,
    tt_tlb_t *tlb,
    const tt_tlb_config_t *cfg
);

/**
 * @brief Free a TLB window.
 *
 * Releases the TLB window and unmaps its memory region.
 *
 * @param dev   Device handle.
 * @param tlb   TLB handle.
 * @return      `TT_OK` or error code.
 */
tt_result_t tt_tlb_free(
    tt_device_t *dev,
    const tt_tlb_t *tlb
);

/*============================================================================
 * MESSAGING
 *
 * ARC is the embedded controller managing firmware, power, and
 * clocks.
 *============================================================================*/

/**
 * @brief ARC message.
 */
typedef struct tt_arc_msg {
    /**
     * @brief Message code.
     */
    uint8_t code;
    /**
     * @brief Message data.
     */
    uint32_t data[8];
} tt_arc_msg_t;

/**
 * @brief Send a message to ARC.
 *
 * @param dev         Device handle.
 * @param msg[in,out] ARC message body.
 * @param wait        Wait for completion.
 * @param timeout     Timeout in milliseconds (`0` for default 1000ms).
 * @return            `TT_OK` or error code.
 */
tt_result_t tt_arc_msg(
    tt_device_t *dev,
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
    /**
     * @brief High part of the board ID.
     */
    TT_TAG_BOARD_ID_HIGH        = 1,
    /**
     * @brief Low part of the board ID.
     */
    TT_TAG_BOARD_ID_LOW         = 2,
    /**
     * @brief ASIC ID.
     */
    TT_TAG_ASIC_ID              = 3,
    /**
     * @brief Harvesting state of the system.
     */
    TT_TAG_HARVESTING_STATE     = 4,
    /**
     * @brief Update interval for telemetry in milliseconds.
     */
    TT_TAG_UPDATE_TELEM_SPEED   = 5,
    /**
     * @brief VCore voltage in millivolts.
     */
    TT_TAG_VCORE                = 6,
    /**
     * @brief Thermal design power (TDP) in watts.
     */
    TT_TAG_TDP                  = 7,
    /**
     * @brief Thermal design current (TDC) in amperes.
     */
    TT_TAG_TDC                  = 8,
    /**
     * @brief VDD limits (min and max) in millivolts.
     */
    TT_TAG_VDD_LIMITS           = 9,
    /**
     * @brief Thermal shutdown limit in degrees Celsius.
     */
    TT_TAG_THM_LIMIT_SHUTDOWN   = 10,
    /**
     * @brief ASIC temperature in signed 16.16 fixed-point format.
     */
    TT_TAG_ASIC_TEMPERATURE     = 11,
    /**
     * @brief Voltage regulator temperature in degrees Celsius.
     * (Not implemented)
     */
    TT_TAG_VREG_TEMPERATURE     = 12,
    /**
     * @brief Board temperature in degrees Celsius.
     * (Not implemented)
     */
    TT_TAG_BOARD_TEMPERATURE    = 13,
    /**
     * @brief AI clock frequency in megahertz.
     */
    TT_TAG_AICLK                = 14,
    /**
     * @brief AXI clock frequency in megahertz.
     */
    TT_TAG_AXICLK               = 15,
    /**
     * @brief ARC clock frequency in megahertz.
     */
    TT_TAG_ARCCLK               = 16,
    /**
     * @brief L2CPU clock 0 frequency in megahertz.
     */
    TT_TAG_L2CPUCLK0            = 17,
    /**
     * @brief L2CPU clock 1 frequency in megahertz.
     */
    TT_TAG_L2CPUCLK1            = 18,
    /**
     * @brief L2CPU clock 2 frequency in megahertz.
     */
    TT_TAG_L2CPUCLK2            = 19,
    /**
     * @brief L2CPU clock 3 frequency in megahertz.
     */
    TT_TAG_L2CPUCLK3            = 20,
    /**
     * @brief Ethernet live status.
     */
    TT_TAG_ETH_LIVE_STATUS      = 21,
    /**
     * @brief GDDR status.
     */
    TT_TAG_GDDR_STATUS          = 22,
    /**
     * @brief GDDR speed in megabits per second.
     */
    TT_TAG_GDDR_SPEED           = 23,
    /**
     * @brief Ethernet firmware version.
     */
    TT_TAG_ETH_FW_VERSION       = 24,
    /**
     * @brief GDDR firmware version.
     */
    TT_TAG_GDDR_FW_VERSION      = 25,
    /**
     * @brief DM application firmware version.
     */
    TT_TAG_DM_APP_FW_VERSION    = 26,
    /**
     * @brief DM bootloader firmware version.
     */
    TT_TAG_DM_BL_FW_VERSION     = 27,
    /**
     * @brief Flash bundle version.
     */
    TT_TAG_FLASH_BUNDLE_VERSION = 28,
    /**
     * @brief CM firmware version.
     */
    TT_TAG_CM_FW_VERSION        = 29,
    /**
     * @brief L2CPU firmware version.
     */
    TT_TAG_L2CPU_FW_VERSION     = 30,
    /**
     * @brief Fan speed as a percentage.
     */
    TT_TAG_FAN_SPEED            = 31,
    /**
     * @brief Timer heartbeat counter.
     */
    TT_TAG_TIMER_HEARTBEAT      = 32,
    /**
     * @brief Total number of telemetry tags.
     */
    TT_TAG_TELEM_ENUM_COUNT     = 33,
    /**
     * @brief Enabled Tensix columns.
     */
    TT_TAG_ENABLED_TENSIX_COL   = 34,
    /**
     * @brief Enabled Ethernet interfaces.
     */
    TT_TAG_ENABLED_ETH          = 35,
    /**
     * @brief Enabled GDDR interfaces.
     */
    TT_TAG_ENABLED_GDDR         = 36,
    /**
     * @brief Enabled L2CPU cores.
     */
    TT_TAG_ENABLED_L2CPU        = 37,
    /**
     * @brief PCIe usage information.
     */
    TT_TAG_PCIE_USAGE           = 38,
    /**
     * @brief Input current in amperes.
     */
    TT_TAG_INPUT_CURRENT        = 39,
    /**
     * @brief NOC translation status.
     */
    TT_TAG_NOC_TRANSLATION      = 40,
    /**
     * @brief Fan RPM.
     */
    TT_TAG_FAN_RPM              = 41,
    /**
     * @brief GDDR 0 and 1 temperature.
     */
    TT_TAG_GDDR_0_1_TEMP        = 42,
    /**
     * @brief GDDR 2 and 3 temperature.
     */
    TT_TAG_GDDR_2_3_TEMP        = 43,
    /**
     * @brief GDDR 4 and 5 temperature.
     */
    TT_TAG_GDDR_4_5_TEMP        = 44,
    /**
     * @brief GDDR 6 and 7 temperature.
     */
    TT_TAG_GDDR_6_7_TEMP        = 45,
    /**
     * @brief GDDR 0 and 1 corrected errors.
     */
    TT_TAG_GDDR_0_1_CORR_ERRS   = 46,
    /**
     * @brief GDDR 2 and 3 corrected errors.
     */
    TT_TAG_GDDR_2_3_CORR_ERRS   = 47,
    /**
     * @brief GDDR 4 and 5 corrected errors.
     */
    TT_TAG_GDDR_4_5_CORR_ERRS   = 48,
    /**
     * @brief GDDR 6 and 7 corrected errors.
     */
    TT_TAG_GDDR_6_7_CORR_ERRS   = 49,
    /**
     * @brief GDDR uncorrected errors.
     */
    TT_TAG_GDDR_UNCORR_ERRS     = 50,
    /**
     * @brief Maximum GDDR temperature.
     */
    TT_TAG_MAX_GDDR_TEMP        = 51,
    /**
     * @brief ASIC location.
     */
    TT_TAG_ASIC_LOCATION        = 52,
    /**
     * @brief Board power limit in watts.
     */
    TT_TAG_BOARD_POWER_LIMIT    = 53,
    /**
     * @brief Input power in watts.
     */
    TT_TAG_INPUT_POWER          = 54,
    /**
     * @brief Maximum TDC limit in amperes.
     */
    TT_TAG_TDC_LIMIT_MAX        = 55,
    /**
     * @brief Thermal throttle limit in degrees Celsius.
     */
    TT_TAG_THM_LIMIT_THROTTLE   = 56,
    /**
     * @brief Firmware build date.
     */
    TT_TAG_FW_BUILD_DATE        = 57,
    /**
     * @brief TT flash version.
     */
    TT_TAG_TT_FLASH_VERSION     = 58,
    /**
     * @brief Enabled Tensix rows.
     */
    TT_TAG_ENABLED_TENSIX_ROW   = 59,
    /**
     * @brief Thermal trip count.
     */
    TT_TAG_THERM_TRIP_COUNT     = 60,
    /**
     * @brief High part of the ASIC ID.
     */
    TT_TAG_ASIC_ID_HIGH         = 61,
    /**
     * @brief Low part of the ASIC ID.
     */
    TT_TAG_ASIC_ID_LOW          = 62,
    /**
     * @brief Maximum AI clock frequency.
     */
    TT_TAG_AICLK_LIMIT_MAX      = 63,
    /**
     * @brief Maximum TDP limit in watts.
     */
    TT_TAG_TDP_LIMIT_MAX        = 64,
    /**
     * @brief Effective minimum AICLK arbiter value in megahertz.
     *
     * This represents the highest frequency requested by all enabled
     * minimum arbiters. Multiple arbiters may request minimum
     * frequencies, and the highest value is effective.
     *
     * @see @ref aiclk_arb_min
     */
    TT_TAG_AICLK_ARB_MIN        = 65,
    /**
     * @brief Effective maximum AICLK arbiter value in megahertz.
     *
     * This represents the lowest frequency limit imposed by all
     * enabled maximum arbiters. Multiple arbiters may impose maximum
     * frequency limits (e.g., TDP, TDC, thermal throttling), and the
     * lowest (most restrictive) value is effective. This value takes
     * precedence over TT_TAG_AICLK_ARB_MIN when determining the
     * final target frequency.
     *
     * @see @ref aiclk_arb_max
     */
    TT_TAG_AICLK_ARB_MAX        = 66,
    /**
     * @brief Bitmask of enabled minimum arbiters.
     *
     * Each bit represents whether a specific minimum frequency
     * arbiter is currently enabled. Bit positions correspond to the
     * values in @ref aiclk_arb_min.
     *
     * @see @ref aiclk_arb_min
     */
    TT_TAG_ENABLED_MIN_ARB      = 67,
    /**
     * @brief Bitmask of enabled maximum arbiters.
     *
     * Each bit represents whether a specific maximum frequency
     * arbiter is currently enabled. Bit positions correspond to the
     * values in @ref aiclk_arb_max.
     *
     * @see @ref aiclk_arb_max
     */
    TT_TAG_ENABLED_MAX_ARB      = 68,
    // Sentinel value for telemetry array length.
    TT_TELEMETRY_LEN,
} tt_telemetry_tag_t;

/**
 * @brief Telemetry data.
 *
 * Array indexed by `tt_telemetry_tag_t`.
 */
typedef uint32_t tt_telemetry_t[TT_TELEMETRY_LEN];

/**
 * @brief Read telemetry from device. (Want a complete snapshot, not partial, no
 * racing)
 *
 * @param dev          Device handle.
 * @param[out] table   Telemetry data output.
 * @return             `TT_OK` or error code.
 */
tt_result_t tt_get_telemetry(tt_device_t *dev, tt_telemetry_t table);

/*============================================================================
 * RESET
 *
 * Device reset operations.
 *============================================================================*/

/**
 * @brief Trigger device reset.
 *
 * Initiates a reset sequence. Closes any open file descriptor in
 * `dev` and opens a dedicated temporary fd for the reset operation
 * (reset should work even if existing fd is corrupted). After reset,
 * the device may need time to reinitialize before becoming available
 * again. All TLBs are invalidated by reset.
 *
 * @param dev   Device handle.
 * @return      `TT_OK` or error code.
 */
tt_result_t tt_reset(tt_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* TT_DAL_H */
