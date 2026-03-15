// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ttdal.h
 * @brief Tenstorrent Device Access Library.
 *
 * This header defines a stateless, mechanism-only C API for accessing
 * Tenstorrent accelerator hardware. It is designed to be consumed by
 * higher-level libraries.
 *
 * @version 0.1.0
 * @copyright Copyright (c) 2026 Tenstorrent Inc.
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

/*============================================================================*
 * VERSION                                                                    *
 *============================================================================*/

/// Major version definition.
#define TT_VERSION_MAJOR 0
/// Minor version definition.
#define TT_VERSION_MINOR 1
/// Patch version definition.
#define TT_VERSION_PATCH 0

/// Major version number.
extern const unsigned int tt_version_major;
/// Minor version number.
extern const unsigned int tt_version_minor;
/// Patch version number.
extern const unsigned int tt_version_patch;

/// Get the API version at runtime.
///
/// Use this to verify ABI compatibility at runtime.
///
/// @param[out] major  Major version number.
/// @param[out] minor  Minor version number.
/// @param[out] patch  Patch version number.
static inline void tt_get_version(int *major, int *minor, int *patch) {
    if (major)
        *major = TT_VERSION_MAJOR;
    if (minor)
        *minor = TT_VERSION_MINOR;
    if (patch)
        *patch = TT_VERSION_PATCH;
}

/*============================================================================*
 * ERRORS                                                                     *
 *============================================================================*/

/// Return value indicating success.
#define TT_OK 0

/// Return value indicating failure.
#define TT_ERR (-1)

/// Error codes.
///
/// Functions return -1 on error and set the thread-local `tt_errno` to the
/// corresponding error code. On success, functions return 0 or a meaningful
/// non-negative value where applicable.
///
/// Error codes are positive integers organized according to the following
/// categories:
///
/// * 100-199: General
/// * 200-299: Device
/// * 300-399: Transport
/// * 400-499: Hardware
///
/// @par Example
///
/// ```c
/// return tt_errno = TT_ENODEV, TT_ERR;
/// ```
/// This returns `TT_ERR` (-1) to the caller while setting `tt_errno` to
/// `TT_ENODEV` (200).
typedef enum tt_error {
    /* General errors (100-199) */
    /// Invalid argument.
    TT_EINVAL = 100,
    /// Out of memory.
    TT_ENOMEM = 101,
    /// Operation not supported.
    TT_ENOTSUP = 102,
    /// No buffer space available.
    TT_ENOBUFS = 103,
    /// Alignment error.
    TT_EALIGN = 104,
    /// I/O error.
    TT_EIO = 105,

    /* Device errors (200-299) */
    /// No such device.
    TT_ENODEV = 200,
    /// Device or resource busy.
    TT_EBUSY = 201,
    /// Device not open.
    TT_ENOTOPEN = 202,
    /// Device lost.
    TT_EDEVLOST = 203,
    /// Device hung.
    TT_EDEVHUNG = 204,
    /// Unsupported architecture.
    TT_EBADARCH = 205,
    /// Permission denied.
    TT_EACCES = 206,

    /* Transport errors (300-399) */
    /// Operation timed out.
    TT_ETIMEDOUT = 300,
    /// ARC message failed.
    TT_EARCMSG = 301,

    /* Hardware state errors (400-499) */
    /// Device not ready.
    TT_ENOTREADY = 400,
} tt_error_t;

/// Thread-local error descriptor.
///
/// Set when an operation fails. Check this for detailed error information when
/// a function returns an error. Similar to the C standard library's `errno`.
///
/// This is thread-local storage.
extern _Thread_local tt_error_t tt_errno;

/// Get a human-readable error message.
///
/// @param error  Error variant.
/// @return       Error string; `NULL` if `TT_OK`.
static inline const char *tt_error_describe(tt_error_t error) {
    // Not an error
    if (!error)
        return NULL;
    // Describe error
    switch (error) {
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

/*============================================================================*
 * DEVICE                                                                     *
 *============================================================================*/

/// Device architecture.
///
/// Values are PCI device IDs.
typedef enum tt_arch {
    /// Grayskull.
    TT_ARCH_GRAYSKULL
        [[deprecated]] = 0xffa0,
    /// Wormhole.
    TT_ARCH_WORMHOLE   = 0x401e,
    /// Blackhole.
    TT_ARCH_BLACKHOLE  = 0xb140,
} tt_arch_t;

/// Get architecture name as string.
///
/// @param arch  Architecture variant.
/// @return      Architecture string; `NULL` for invalid.
static inline const char *tt_arch_describe(tt_arch_t arch) {
    switch (arch) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        case TT_ARCH_GRAYSKULL:
            return "Grayskull";
#pragma GCC diagnostic pop
        case TT_ARCH_WORMHOLE:
            return "Wormhole";
        case TT_ARCH_BLACKHOLE:
            return "Blackhole";
        default:
            return NULL;
    }
}

/// Device handle.
///
/// A handle that identifies a device and provides means for various supported
/// operations.
///
/// These are obtained by calling `tt_device_discover()`. In order to use the
/// device, it must have been opened with `tt_device_open()`. Use
/// `tt-device_close()` to clean up open devices.
typedef struct tt_device {
    /// Device identifier.
    ///
    /// Uniquely identifies a device. Suitable for comparisons, as multiple
    /// instances of the same device will share this identifier,
    ///
    /// TODO: This value should remain stable after a reset, although the
    /// current implementation does not reflect this. This could be accomplished
    /// by using a driver-provided UUID rather than using the device number.
    ///
    /// NOTE: This choice of type is unstable, and may be changed without
    /// warning before the initial release.
    uint32_t id;
    /// File descriptor.
    ///
    /// A handle to the underlying device owned by this device instance. It is
    /// used to perform operations on the device. When not opened, the value is
    /// `-1`.
    int fd;
} tt_device_t;

/// Create device from path.
///
/// Must call `tt_device_open()` before using the device.
///
/// @param path      Device path.
/// @param[out] dev  Device handle to initialize.
/// @return          0 on success, -1 on error (check `tt_errno`).
///
/// @par Example
///
/// ```c
/// tt_device_t dev;
/// if (tt_device_from_path("/dev/tenstorrent/0", &dev) < 0) {
///     // Handle error
/// }
/// tt_device_open(&dev);
/// // ... use device ...
/// tt_device_close(&dev);
/// ```
int tt_device_new(const char *path, tt_device_t *dev);

/// Discover connected devices.
///
/// Scans for connected devices. Fills buffer with up to `cap` devices. Return
/// value may exceed `cap` if more devices were found.
///
/// Must call `tt_device_open()` before using a device obtained this way.
///
/// @param cap       Capacity of output buffer.
/// @param[out] buf  Buffer for found devices.
/// @return          Number of devices found (may exceed `cap`), negative on
///                  error.
///
/// @par Example
///
/// ```c
/// tt_device_t devs[16];
/// ssize_t count = tt_device_discover(16, devs);
/// if (count < 0)
///     return count;
///
/// size_t actual = (count < 16) ? count : 16;
/// for (size_t i = 0; i < actual; i++) {
///     tt_device_open(&devs[i]);
///     // ... use device ...
///     tt_device_close(&devs[i]);
/// }
/// ```
ssize_t tt_device_discover(size_t cap, tt_device_t buf[static cap]);

/// Open device.
///
/// Opens the device, after which it can be used. Multiple calls are safe.
/// Results in a NOP if not already open.
///
/// @param dev   Device handle.
/// @return      0 on success, -1 on error (check `tt_errno`).
int tt_device_open(tt_device_t *dev);

/// Close device.
///
/// Closes the device, after which it can no longer be used. Multiple calls are
/// safe. Results in a NOP if already open.
///
/// @param dev   Device handle.
/// @return      0 on success, -1 on error (check `tt_errno`).
int tt_device_close(tt_device_t *dev);

/// Device information.
///
/// Contains a snapshot of static information about a device.
typedef struct tt_device_info {
    /// Size of output structure.
    uint32_t output_size_bytes;
    /// PCI vendor ID.
    uint16_t vendor_id;
    /// PCI device ID.
    uint16_t device_id;
    /// PCI subsystem vendor ID.
    uint16_t subsystem_vendor_id;
    /// PCI subsystem ID.
    uint16_t subsystem_id;
    /// PCI BDF (bus/device/function).
    uint16_t bus_dev_fn;
    /// Max DMA buffer size (log2).
    uint16_t max_dma_buf_size_log2;
    /// PCI domain.
    uint16_t pci_domain;
} tt_device_info_t;

/// Fetch device information.
///
/// Gets static information about a device. Guaranteed not to change during the
/// device lifecycle.
///
/// @param dev       Device handle.
/// @param[out] info Device information output.
/// @return          0 on success, -1 on error (check `tt_errno`).
int tt_get_device_info(tt_device_t *dev, tt_device_info_t *info);

/*============================================================================*
 * ADDRESSING                                                                 *
 *============================================================================*/

/*
 * Translation Lookaside Buffer (TLB) windows provide direct memory-mapped
 * access to device NOC addresses. These are fixed-size apertures that
 * transparently translate host memory operations to device transactions.
 */

/// TLB size.
///
/// Valid TLB window sizes. Not all sizes are available on all architectures;
/// kernel validates availability.
typedef enum tt_tlb_size {
    /// 1MB window.
    ///
    /// Supported:
    /// - Wormhole
    TT_TLB_1MB  = (1UL << 20),
    /// 2MB window.
    ///
    /// Supported:
    /// - Wormhole
    /// - Blackhole
    TT_TLB_2MB  = (1UL << 21),
    /// 16MB window.
    ///
    /// Supported:
    /// - Wormhole
    TT_TLB_16MB = (1UL << 24),
    /// 4GB window.
    ///
    /// Supported:
    /// - Blackhole
    TT_TLB_4GB  = (1UL << 32),
} tt_tlb_size_t;

/// TLB handle.
///
/// Contains TLB identifier, mapped pointer, and size. Allocated by
/// `tt_tlb_alloc()` and freed by `tt_tlb_free()`. The `ptr` field is `NULL`
/// until `tt_tlb_configure()` is called.
typedef struct tt_tlb {
    /// TLB identifier.
    uint32_t id;
    /// Memory-mapped window (`NULL` until configured).
    void *ptr;
    /// Window size in bytes.
    size_t len;
    /// Memory-map offset (for internal use).
    uint64_t idx;
} tt_tlb_t;

/// TLB cache mode.
typedef enum tt_tlb_cache_mode {
    /// Uncached.
    ///
    /// Use for register access where ordering and immediate visibility matter.
    TT_TLB_UC = 0,
    /// Write-combined.
    ///
    /// Use for memory access where batching writes improves performance.
    TT_TLB_WC = 1,
} tt_tlb_cache_mode_t;

/// TLB NOC configuration.
///
/// Specifies the NOC target and address mapping for a TLB window.
typedef struct tt_tlb_config {
    /// Device address.
    uint64_t addr;
    /// Target X coordinate.
    uint8_t x_end;
    /// Target Y coordinate.
    uint8_t y_end;
    /// Multicast start X.
    uint8_t x_start;
    /// Multicast start Y.
    uint8_t y_start;
    /// NOC selector (`0` or `1`).
    uint8_t noc;
    /// Multicast enable.
    bool mcast;
    /// Linked TLB flag.
    bool linked;
    /// Static virtual channel.
    uint8_t static_vc;
} tt_tlb_config_t;

/// Allocate a TLB window.
///
/// Allocates a TLB of the requested size from the device. The kernel validates
/// size availability for the device architecture. The returned TLB will not yet
/// have a pointer (`NULL`) and must be configured with `tt_tlb_configure()`
/// before use.
///
/// @param dev      Device handle.
/// @param size     Window size.
/// @param mode     Cache mode.
/// @param[out] tlb Allocated TLB handle with `ptr = NULL`.
/// @return         0 on success, -1 on error (check `tt_errno`).
int tt_tlb_alloc(
    tt_device_t *dev,
    tt_tlb_size_t size,
    tt_tlb_cache_mode_t mode,
    tt_tlb_t *tlb
);

/// Configure a TLB address mapping.
///
/// Sets the NOC target address and coordinates, mapping the TLB into the
/// process address space and setting the pointer.
///
/// Calling with an already-mapped TLB to remap will invalidate stale interior
/// pointers (fail-fast on misuse).
///
/// @param dev   Device handle.
/// @param tlb   TLB handle.
/// @param cfg   NOC configuration.
/// @return      0 on success, -1 on error (check `tt_errno`).
int tt_tlb_configure(
    tt_device_t *dev,
    tt_tlb_t *tlb,
    const tt_tlb_config_t *cfg
);

/// Free a TLB window.
///
/// Releases the TLB window and unmaps its memory region.
///
/// @param dev   Device handle.
/// @param tlb   TLB handle.
/// @return      0 on success, -1 on error (check `tt_errno`).
int tt_tlb_free(
    tt_device_t *dev,
    tt_tlb_t *tlb
);

/*============================================================================*
 * MESSAGING                                                                  *
 *                                                                            *
 * ARC is the embedded controller managing firmware, power, and              *
 * clocks.                                                                    *
 *============================================================================*/

/// ARC message.
typedef struct tt_arc_msg {
    /// Message code.
    uint8_t code;
    /// Message data.
    uint32_t data[8];
} tt_arc_msg_t;

/// Send a message to ARC.
///
/// @param dev            Device handle.
/// @param[in,out] msg    ARC message body.
/// @param wait           Wait for completion.
/// @param timeout        Timeout in milliseconds (`0` for default 1000ms).
/// @return               0 on success, -1 on error (check `tt_errno`).
int tt_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
);

/*============================================================================*
 * TELEMETRY                                                                  *
 *                                                                            *
 * Raw telemetry data read from device.                                      *
 *============================================================================*/

/// Telemetry tags.
typedef enum tt_telemetry_tag {
    /// High part of the board ID.
    TT_TAG_BOARD_ID_HIGH        = 1,
    /// Low part of the board ID.
    TT_TAG_BOARD_ID_LOW         = 2,
    /// ASIC ID.
    TT_TAG_ASIC_ID              = 3,
    /// Harvesting state of the system.
    TT_TAG_HARVESTING_STATE     = 4,
    /// Update interval for telemetry in milliseconds.
    TT_TAG_UPDATE_TELEM_SPEED   = 5,
    /// VCore voltage in millivolts.
    TT_TAG_VCORE                = 6,
    /// Thermal design power (TDP) in watts.
    TT_TAG_TDP                  = 7,
    /// Thermal design current (TDC) in amperes.
    TT_TAG_TDC                  = 8,
    /// VDD limits (min and max) in millivolts.
    TT_TAG_VDD_LIMITS           = 9,
    /// Thermal shutdown limit in degrees Celsius.
    TT_TAG_THM_LIMIT_SHUTDOWN   = 10,
    /// ASIC temperature in signed 16.16 fixed-point format.
    TT_TAG_ASIC_TEMPERATURE     = 11,
    /// Voltage regulator temperature in degrees Celsius (not implemented).
    TT_TAG_VREG_TEMPERATURE     = 12,
    /// Board temperature in degrees Celsius (not implemented).
    TT_TAG_BOARD_TEMPERATURE    = 13,
    /// AI clock frequency in megahertz.
    TT_TAG_AICLK                = 14,
    /// AXI clock frequency in megahertz.
    TT_TAG_AXICLK               = 15,
    /// ARC clock frequency in megahertz.
    TT_TAG_ARCCLK               = 16,
    /// L2CPU clock 0 frequency in megahertz.
    TT_TAG_L2CPUCLK0            = 17,
    /// L2CPU clock 1 frequency in megahertz.
    TT_TAG_L2CPUCLK1            = 18,
    /// L2CPU clock 2 frequency in megahertz.
    TT_TAG_L2CPUCLK2            = 19,
    /// L2CPU clock 3 frequency in megahertz.
    TT_TAG_L2CPUCLK3            = 20,
    /// Ethernet live status.
    TT_TAG_ETH_LIVE_STATUS      = 21,
    /// GDDR status.
    TT_TAG_GDDR_STATUS          = 22,
    /// GDDR speed in megabits per second.
    TT_TAG_GDDR_SPEED           = 23,
    /// Ethernet firmware version.
    TT_TAG_ETH_FW_VERSION       = 24,
    /// GDDR firmware version.
    TT_TAG_GDDR_FW_VERSION      = 25,
    /// DM application firmware version.
    TT_TAG_DM_APP_FW_VERSION    = 26,
    /// DM bootloader firmware version.
    TT_TAG_DM_BL_FW_VERSION     = 27,
    /// Flash bundle version.
    TT_TAG_FLASH_BUNDLE_VERSION = 28,
    /// CM firmware version.
    TT_TAG_CM_FW_VERSION        = 29,
    /// L2CPU firmware version.
    TT_TAG_L2CPU_FW_VERSION     = 30,
    /// Fan speed as a percentage.
    TT_TAG_FAN_SPEED            = 31,
    /// Timer heartbeat counter.
    TT_TAG_TIMER_HEARTBEAT      = 32,
    /// Total number of telemetry tags.
    TT_TAG_TELEM_ENUM_COUNT     = 33,
    /// Enabled Tensix columns.
    TT_TAG_ENABLED_TENSIX_COL   = 34,
    /// Enabled Ethernet interfaces.
    TT_TAG_ENABLED_ETH          = 35,
    /// Enabled GDDR interfaces.
    TT_TAG_ENABLED_GDDR         = 36,
    /// Enabled L2CPU cores.
    TT_TAG_ENABLED_L2CPU        = 37,
    /// PCIe usage information.
    TT_TAG_PCIE_USAGE           = 38,
    /// Input current in amperes.
    TT_TAG_INPUT_CURRENT        = 39,
    /// NOC translation status.
    TT_TAG_NOC_TRANSLATION      = 40,
    /// Fan RPM.
    TT_TAG_FAN_RPM              = 41,
    /// GDDR 0 and 1 temperature.
    TT_TAG_GDDR_0_1_TEMP        = 42,
    /// GDDR 2 and 3 temperature.
    TT_TAG_GDDR_2_3_TEMP        = 43,
    /// GDDR 4 and 5 temperature.
    TT_TAG_GDDR_4_5_TEMP        = 44,
    /// GDDR 6 and 7 temperature.
    TT_TAG_GDDR_6_7_TEMP        = 45,
    /// GDDR 0 and 1 corrected errors.
    TT_TAG_GDDR_0_1_CORR_ERRS   = 46,
    /// GDDR 2 and 3 corrected errors.
    TT_TAG_GDDR_2_3_CORR_ERRS   = 47,
    /// GDDR 4 and 5 corrected errors.
    TT_TAG_GDDR_4_5_CORR_ERRS   = 48,
    /// GDDR 6 and 7 corrected errors.
    TT_TAG_GDDR_6_7_CORR_ERRS   = 49,
    /// GDDR uncorrected errors.
    TT_TAG_GDDR_UNCORR_ERRS     = 50,
    /// Maximum GDDR temperature.
    TT_TAG_MAX_GDDR_TEMP        = 51,
    /// ASIC location.
    TT_TAG_ASIC_LOCATION        = 52,
    /// Board power limit in watts.
    TT_TAG_BOARD_POWER_LIMIT    = 53,
    /// Input power in watts.
    TT_TAG_INPUT_POWER          = 54,
    /// Maximum TDC limit in amperes.
    TT_TAG_TDC_LIMIT_MAX        = 55,
    /// Thermal throttle limit in degrees Celsius.
    TT_TAG_THM_LIMIT_THROTTLE   = 56,
    /// Firmware build date.
    TT_TAG_FW_BUILD_DATE        = 57,
    /// TT flash version.
    TT_TAG_TT_FLASH_VERSION     = 58,
    /// Enabled Tensix rows.
    TT_TAG_ENABLED_TENSIX_ROW   = 59,
    /// Thermal trip count.
    TT_TAG_THERM_TRIP_COUNT     = 60,
    /// High part of the ASIC ID.
    TT_TAG_ASIC_ID_HIGH         = 61,
    /// Low part of the ASIC ID.
    TT_TAG_ASIC_ID_LOW          = 62,
    /// Maximum AI clock frequency.
    TT_TAG_AICLK_LIMIT_MAX      = 63,
    /// Maximum TDP limit in watts.
    TT_TAG_TDP_LIMIT_MAX        = 64,
    /// Effective minimum AICLK arbiter value in megahertz.
    ///
    /// This represents the highest frequency requested by all enabled
    /// minimum arbiters. Multiple arbiters may request minimum
    /// frequencies, and the highest value is effective.
    TT_TAG_AICLK_ARB_MIN        = 65,
    /// Effective maximum AICLK arbiter value in megahertz.
    ///
    /// This represents the lowest frequency limit imposed by all
    /// enabled maximum arbiters. Multiple arbiters may impose maximum
    /// frequency limits (e.g., TDP, TDC, thermal throttling), and the
    /// lowest (most restrictive) value is effective. This value takes
    /// precedence over TT_TAG_AICLK_ARB_MIN when determining the
    /// final target frequency.
    TT_TAG_AICLK_ARB_MAX        = 66,
    /// Bitmask of enabled minimum arbiters.
    ///
    /// Each bit represents whether a specific minimum frequency arbiter is
    /// currently enabled.
    TT_TAG_ENABLED_MIN_ARB      = 67,
    /// Bitmask of enabled maximum arbiters.
    ///
    /// Each bit represents whether a specific maximum frequency arbiter is
    /// currently enabled.
    TT_TAG_ENABLED_MAX_ARB      = 68,
    // Sentinel value for telemetry array length.
    TT_TELEMETRY_LEN,
} tt_telemetry_tag_t;

/// Telemetry data.
///
/// Array indexed by `tt_telemetry_tag_t`.
typedef uint32_t tt_telemetry_t[TT_TELEMETRY_LEN];

/// Read telemetry from device.
///
/// Returns a complete snapshot without racing (not partial).
///
/// @param dev          Device handle.
/// @param[out] table   Telemetry data output.
/// @return             0 on success, -1 on error (check `tt_errno`).
int tt_get_telemetry(tt_device_t *dev, tt_telemetry_t table);

/*============================================================================*
 * RESET                                                                      *
 *                                                                            *
 * Device reset operations.                                                   *
 *============================================================================*/

/// Trigger device reset.
///
/// Initiates a reset sequence. Closes any open file descriptor in `dev` and
/// opens a dedicated temporary fd for the reset operation (reset should work
/// even if existing fd is corrupted). After reset, the device may need time to
/// reinitialize before becoming available again. All TLBs are invalidated by
/// reset.
///
/// @param dev   Device handle.
/// @return      0 on success, -1 on error (check `tt_errno`).
int tt_reset(tt_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* TT_DAL_H */
