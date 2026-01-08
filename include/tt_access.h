// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tt_access.h
 * @brief Tenstorrent Hardware Access API
 *
 * This header defines a stateless, mechanism-only C API for accessing
 * Tenstorrent AI accelerator hardware. It is designed to be implemented
 * by UMD (User Mode Driver) and consumed by higher-level libraries.
 *
 * =============================================================================
 * DESIGN PRINCIPLES
 * =============================================================================
 *
 * - 100% MECHANISM, 0% POLICY
 *   This API provides raw access to hardware. It does not decide when or why
 *   to perform operations - that is the responsibility of higher-level APIs.
 *
 * - STATELESS
 *   No opaque handles or implicit state. All operations take explicit device
 *   IDs and transport specifications. No open/close lifecycle to manage.
 *
 * - COMPOSABLE TRANSPORTS
 *   Hardware access is through explicit transport layers (PCIe BAR, AXI, etc.)
 *   that can be composed and selected per-operation.
 *
 * - IP-BLOCK CENTRIC
 *   The chip is modeled as a composition of IP blocks (ARC, NOC, SPI, etc.)
 *   rather than a monolithic device abstraction.
 *
 * =============================================================================
 * TABLE OF CONTENTS
 * =============================================================================
 *
 * 1. VERSION INFORMATION
 *    - API version macros and runtime version query
 *
 * 2. ERROR HANDLING
 *    - tt_result_t: Comprehensive error codes by category
 *    - tt_result_to_string(): Human-readable error messages
 *
 * 3. FUNDAMENTAL TYPES
 *    - tt_device_id_t: Device identifier (stateless)
 *    - tt_arch_t: Device architecture enum
 *    - tt_noc_coord_t: NOC core coordinates
 *    - tt_eth_addr_t: Ethernet address for multi-chip systems
 *
 * 4. TRANSPORT MODEL
 *    - tt_transport_t: Transport kind enum (PCIe BAR, AXI, JTAG)
 *    - Generic read/write through any transport
 *    - Batched operations (readv/writev) for performance
 *
 * 5. DEVICE ENUMERATION
 *    - tt_device_descriptor_t: Device info (PCIe BDF, arch, board ID)
 *    - tt_enumerate_devices(): Discover all devices
 *    - tt_get_device_descriptor(): Get info for specific device
 *
 * 6. REGISTER ACCESS (Convenience Wrappers)
 *    - 32-bit and 64-bit read/write helpers
 *    - Block read/write for larger transfers
 *
 * 7. AXI ACCESS (ARC Address Space)
 *    - tt_axi_read/write: ARC coprocessor memory access
 *    - Syntactic sugar over transport layer
 *
 * 8. NOC ACCESS (Network-on-Chip)
 *    - tt_noc_read/write: Per-core memory access
 *    - tt_noc_broadcast/multicast: Multi-core writes
 *
 * 9. ARC MESSAGING
 *    - tt_arc_msg(): Send command to ARC controller
 *    - tt_arc_msg_extended(): Extended 8-argument messages
 *
 * 10. TELEMETRY
 *     - tt_telemetry_t: Comprehensive monitoring data
 *     - tt_get_telemetry(): Read all monitoring data
 *
 * 11. SPI FLASH ACCESS
 *     - tt_spi_read/write/erase: Raw flash operations
 *     - tt_spi_get_info(): Query flash parameters
 *
 * 12. REMOTE CHIP ACCESS (Ethernet)
 *     - tt_neighbor_t: Neighbor information
 *     - tt_get_neighbors(): Discover connected chips
 *     - tt_eth_noc_*: Remote NOC operations via Ethernet
 *
 * 13. RESET OPERATIONS (Mechanism Only)
 *     - tt_reset(): Raw reset sequences
 *     - tt_ipmi_reset(): Galaxy system IPMI reset
 *
 * 14. BOOT FILESYSTEM (Blackhole)
 *     - tt_boot_fs_entry_t: Boot filesystem entry
 *     - tt_decode_boot_fs_entry(): Decode entry by tag
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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*============================================================================
 * 1. VERSION INFORMATION
 *============================================================================*/

#define TT_API_VERSION_MAJOR 0
#define TT_API_VERSION_MINOR 1
#define TT_API_VERSION_PATCH 0

/**
 * @brief Get the runtime library version.
 *
 * @param[out] major  Major version number
 * @param[out] minor  Minor version number
 * @param[out] patch  Patch version number
 *
 * Use this to verify ABI compatibility at runtime.
 */
void tt_get_version(int *major, int *minor, int *patch);

/*============================================================================
 * 2. ERROR HANDLING
 *============================================================================*/

/**
 * @brief Error codes returned by API functions.
 *
 * All functions return TT_OK (0) on success, or a negative error code on
 * failure. Positive values are reserved for function-specific success codes.
 *
 * Error code ranges:
 *   -1   to  -99: General errors
 *   -100 to -199: Device errors
 *   -200 to -299: Communication/transport errors
 *   -300 to -399: Hardware state errors
 *   -400 to -499: Driver/system errors
 *   -500 to -599: SPI/Flash errors
 *   -600 to -699: Remote chip errors
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
    TT_ERROR_NO_DEVICE          = -100, /**< No device found */
    TT_ERROR_DEVICE_NOT_FOUND   = -101, /**< Specified device not found */
    TT_ERROR_DEVICE_BUSY        = -102, /**< Device is busy */
    TT_ERROR_DEVICE_LOST        = -103, /**< Device removed or unavailable */
    TT_ERROR_DEVICE_HUNG        = -104, /**< Device is unresponsive */
    TT_ERROR_WRONG_ARCH         = -105, /**< Wrong architecture for operation */

    /* Communication/transport errors (-200 to -299) */
    TT_ERROR_TIMEOUT            = -200, /**< Operation timed out */
    TT_ERROR_TRANSPORT_FAILED   = -201, /**< Transport layer failure */
    TT_ERROR_TRANSPORT_INVALID  = -202, /**< Invalid transport for device */
    TT_ERROR_BAR_NOT_MAPPED     = -203, /**< PCIe BAR not mapped */
    TT_ERROR_NOC_ERROR          = -204, /**< NOC communication error */
    TT_ERROR_ARC_MSG_FAILED     = -205, /**< ARC message failed */

    /* Hardware state errors (-300 to -399) */
    TT_ERROR_ARC_NOT_READY      = -300, /**< ARC firmware not ready */
    TT_ERROR_ETH_NOT_TRAINED    = -301, /**< Ethernet links not trained */
    TT_ERROR_DRAM_NOT_TRAINED   = -302, /**< DRAM training not complete */
    TT_ERROR_FW_TOO_OLD         = -303, /**< Firmware version too old */
    TT_ERROR_FW_CORRUPTED       = -304, /**< Firmware appears corrupted */

    /* Driver/system errors (-400 to -499) */
    TT_ERROR_DRIVER_NOT_LOADED  = -400, /**< Kernel driver not loaded */
    TT_ERROR_DRIVER_MISMATCH    = -401, /**< Kernel driver version mismatch */
    TT_ERROR_PERMISSION_DENIED  = -402, /**< Insufficient permissions */
    TT_ERROR_IOCTL_FAILED       = -403, /**< IOCTL call failed */

    /* SPI/Flash errors (-500 to -599) */
    TT_ERROR_SPI_BUSY           = -500, /**< SPI controller busy */
    TT_ERROR_SPI_TIMEOUT        = -501, /**< SPI operation timed out */
    TT_ERROR_SPI_VERIFY_FAILED  = -502, /**< SPI write verification failed */
    TT_ERROR_SPI_PROTECTED      = -503, /**< SPI region is write-protected */

    /* Remote chip errors (-600 to -699) */
    TT_ERROR_REMOTE_UNREACHABLE = -600, /**< Remote chip not reachable */
    TT_ERROR_ROUTE_NOT_FOUND    = -601, /**< No route to remote chip */
    TT_ERROR_ETH_LINK_DOWN      = -602, /**< Ethernet link is down */
} tt_result_t;

/**
 * @brief Get a human-readable error message.
 *
 * @param result  The error code
 * @return        Static string describing the error. Never NULL.
 */
const char *tt_result_to_string(tt_result_t result);

/*============================================================================
 * 3. FUNDAMENTAL TYPES
 *============================================================================*/

/**
 * @brief Device identifier.
 *
 * A simple integer identifying a device. Obtained from enumeration.
 * This is NOT an opaque handle - the API is stateless.
 */
typedef uint32_t tt_device_id_t;

/** Invalid device ID sentinel value. */
#define TT_DEVICE_ID_INVALID ((tt_device_id_t)-1)

/**
 * @brief Device architecture.
 */
typedef enum tt_arch {
    TT_ARCH_UNKNOWN   = 0,  /**< Unknown architecture */
    TT_ARCH_GRAYSKULL = 1,  /**< Grayskull (deprecated) */
    TT_ARCH_WORMHOLE  = 2,  /**< Wormhole */
    TT_ARCH_BLACKHOLE = 3,  /**< Blackhole */
} tt_arch_t;

/**
 * @brief Get architecture name as string.
 */
const char *tt_arch_to_string(tt_arch_t arch);

/**
 * @brief NOC core coordinate.
 */
typedef struct tt_noc_coord {
    uint8_t x;  /**< X coordinate on NOC grid */
    uint8_t y;  /**< Y coordinate on NOC grid */
} tt_noc_coord_t;

/**
 * @brief Ethernet address for multi-chip systems.
 */
typedef struct tt_eth_addr {
    uint8_t shelf_x;    /**< X within shelf */
    uint8_t shelf_y;    /**< Y within shelf */
    uint8_t rack_x;     /**< Rack X coordinate */
    uint8_t rack_y;     /**< Rack Y coordinate */
} tt_eth_addr_t;

/*============================================================================
 * 4. TRANSPORT MODEL
 *
 * Transports provide access to device address spaces. All hardware access
 * goes through an explicit transport specification.
 *============================================================================*/

/**
 * @brief Transport kinds.
 *
 * Specifies which transport/interface to use for an operation.
 */
typedef enum tt_transport {
    TT_TRANSPORT_PCIE_BAR0  = 0,    /**< PCIe BAR 0 (primary) */
    TT_TRANSPORT_PCIE_BAR2  = 1,    /**< PCIe BAR 2 */
    TT_TRANSPORT_PCIE_BAR4  = 2,    /**< PCIe BAR 4 */
    TT_TRANSPORT_AXI        = 3,    /**< AXI bus (via BAR) */
    TT_TRANSPORT_JTAG       = 4,    /**< JTAG interface */
} tt_transport_t;

/**
 * @brief Read operation descriptor for batched reads.
 */
typedef struct tt_read_op {
    uint64_t addr;          /**< Address to read from */
    void *data;             /**< Buffer to receive data */
    size_t size;            /**< Number of bytes to read */
    tt_result_t result;     /**< Per-operation result (output) */
} tt_read_op_t;

/**
 * @brief Write operation descriptor for batched writes.
 */
typedef struct tt_write_op {
    uint64_t addr;          /**< Address to write to */
    const void *data;       /**< Data to write */
    size_t size;            /**< Number of bytes to write */
    tt_result_t result;     /**< Per-operation result (output) */
} tt_write_op_t;

/**
 * @brief Read from a device via specified transport.
 *
 * This is the fundamental read primitive. All other read functions
 * are syntactic sugar built on this.
 *
 * @param dev       Device ID
 * @param transport Transport to use
 * @param addr      Address in transport's address space
 * @param data      Buffer to receive data
 * @param size      Number of bytes to read
 * @return          TT_OK or error code
 */
tt_result_t tt_read(tt_device_id_t dev,
                    tt_transport_t transport,
                    uint64_t addr,
                    void *data,
                    size_t size);

/**
 * @brief Write to a device via specified transport.
 *
 * @param dev       Device ID
 * @param transport Transport to use
 * @param addr      Address in transport's address space
 * @param data      Data to write
 * @param size      Number of bytes to write
 * @return          TT_OK or error code
 */
tt_result_t tt_write(tt_device_id_t dev,
                     tt_transport_t transport,
                     uint64_t addr,
                     const void *data,
                     size_t size);

/**
 * @brief Batched read operations for performance.
 *
 * Performs multiple reads in a single call. Each operation's result
 * is stored in its `result` field. The function returns TT_OK if all
 * operations succeeded, or the first error encountered.
 *
 * @param dev       Device ID
 * @param transport Transport to use
 * @param ops       Array of read operations
 * @param count     Number of operations
 * @return          TT_OK if all succeeded, or first error
 */
tt_result_t tt_readv(tt_device_id_t dev,
                     tt_transport_t transport,
                     tt_read_op_t *ops,
                     size_t count);

/**
 * @brief Batched write operations for performance.
 *
 * @param dev       Device ID
 * @param transport Transport to use
 * @param ops       Array of write operations
 * @param count     Number of operations
 * @return          TT_OK if all succeeded, or first error
 */
tt_result_t tt_writev(tt_device_id_t dev,
                      tt_transport_t transport,
                      tt_write_op_t *ops,
                      size_t count);

/*============================================================================
 * 5. DEVICE ENUMERATION
 *============================================================================*/

/**
 * @brief PCIe device information.
 */
typedef struct tt_pci_info {
    uint16_t domain;        /**< PCI domain */
    uint16_t bus;           /**< PCI bus */
    uint16_t device;        /**< PCI device (slot) */
    uint16_t function;      /**< PCI function */
    uint16_t vendor_id;     /**< PCI vendor ID (0x1E52) */
    uint16_t device_id;     /**< PCI device ID */
    uint16_t subsystem_id;  /**< Subsystem ID (board type) */
    uint64_t bar0_size;     /**< BAR0 size in bytes */
    uint64_t bar2_size;     /**< BAR2 size in bytes */
    uint64_t bar4_size;     /**< BAR4 size in bytes */
} tt_pci_info_t;

/**
 * @brief PCIe link status.
 */
typedef struct tt_pci_link {
    uint8_t current_speed;  /**< Current gen (1=Gen1, 2=Gen2, etc.) */
    uint8_t max_speed;      /**< Maximum supported gen */
    uint8_t current_width;  /**< Current width (1, 4, 8, 16) */
    uint8_t max_width;      /**< Maximum supported width */
} tt_pci_link_t;

/**
 * @brief Device descriptor.
 *
 * Contains all static information about a device. This is purely
 * informational with no policy baked in.
 */
typedef struct tt_device_descriptor {
    tt_device_id_t id;          /**< Device ID for use with API functions */
    tt_arch_t arch;             /**< Device architecture */
    tt_pci_info_t pci;          /**< PCIe information */
    tt_pci_link_t link;         /**< PCIe link status */
    uint64_t board_id;          /**< Board serial number */
    uint16_t board_type;        /**< Board type code */
    char board_name[32];        /**< Human-readable board name */
    int numa_node;              /**< NUMA node (-1 if unknown) */
} tt_device_descriptor_t;

/**
 * @brief Enumerate all Tenstorrent devices.
 *
 * Returns descriptors for all devices found. Call with descriptors=NULL
 * to query count only.
 *
 * @param[out] descriptors  Array to receive descriptors, or NULL
 * @param max_count         Maximum entries in array
 * @return                  Number of devices found (may exceed max_count),
 *                          or negative error code
 *
 * Example:
 * @code
 *     int count = tt_enumerate_devices(NULL, 0);
 *     if (count < 0) return count;
 *
 *     tt_device_descriptor_t *devs = malloc(count * sizeof(*devs));
 *     tt_enumerate_devices(devs, count);
 *
 *     for (int i = 0; i < count; i++) {
 *         printf("Device %u: %s (%s)\n",
 *                devs[i].id,
 *                devs[i].board_name,
 *                tt_arch_to_string(devs[i].arch));
 *     }
 * @endcode
 */
int tt_enumerate_devices(tt_device_descriptor_t *descriptors, size_t max_count);

/**
 * @brief Get descriptor for a specific device.
 *
 * @param dev            Device ID
 * @param[out] descriptor Receives device descriptor
 * @return               TT_OK or error code
 */
tt_result_t tt_get_device_descriptor(tt_device_id_t dev,
                                     tt_device_descriptor_t *descriptor);

/*============================================================================
 * 6. REGISTER ACCESS (Convenience Wrappers)
 *
 * These are thin wrappers over tt_read/tt_write for common register sizes.
 *============================================================================*/

/**
 * @brief Read 32-bit register.
 */
static inline tt_result_t tt_read32(tt_device_id_t dev,
                                    tt_transport_t transport,
                                    uint64_t addr,
                                    uint32_t *value)
{
    return tt_read(dev, transport, addr, value, sizeof(*value));
}

/**
 * @brief Write 32-bit register.
 */
static inline tt_result_t tt_write32(tt_device_id_t dev,
                                     tt_transport_t transport,
                                     uint64_t addr,
                                     uint32_t value)
{
    return tt_write(dev, transport, addr, &value, sizeof(value));
}

/**
 * @brief Read 64-bit register.
 */
static inline tt_result_t tt_read64(tt_device_id_t dev,
                                    tt_transport_t transport,
                                    uint64_t addr,
                                    uint64_t *value)
{
    return tt_read(dev, transport, addr, value, sizeof(*value));
}

/**
 * @brief Write 64-bit register.
 */
static inline tt_result_t tt_write64(tt_device_id_t dev,
                                     tt_transport_t transport,
                                     uint64_t addr,
                                     uint64_t value)
{
    return tt_write(dev, transport, addr, &value, sizeof(value));
}

/*============================================================================
 * 7. AXI ACCESS (ARC Address Space)
 *
 * Convenience wrappers for AXI bus access to ARC coprocessor.
 * These are syntactic sugar over the transport layer.
 *============================================================================*/

/**
 * @brief Read from AXI address space.
 */
static inline tt_result_t tt_axi_read(tt_device_id_t dev,
                                      uint64_t addr,
                                      void *data,
                                      size_t size)
{
    return tt_read(dev, TT_TRANSPORT_AXI, addr, data, size);
}

/**
 * @brief Write to AXI address space.
 */
static inline tt_result_t tt_axi_write(tt_device_id_t dev,
                                       uint64_t addr,
                                       const void *data,
                                       size_t size)
{
    return tt_write(dev, TT_TRANSPORT_AXI, addr, data, size);
}

/**
 * @brief Read 32-bit from AXI.
 */
static inline tt_result_t tt_axi_read32(tt_device_id_t dev,
                                        uint64_t addr,
                                        uint32_t *value)
{
    return tt_read(dev, TT_TRANSPORT_AXI, addr, value, sizeof(*value));
}

/**
 * @brief Write 32-bit to AXI.
 */
static inline tt_result_t tt_axi_write32(tt_device_id_t dev,
                                         uint64_t addr,
                                         uint32_t value)
{
    return tt_write(dev, TT_TRANSPORT_AXI, addr, &value, sizeof(value));
}

/*============================================================================
 * 8. NOC ACCESS (Network-on-Chip)
 *
 * NOC operations access memory on cores via the chip's internal mesh.
 * These require NOC routing and are separate from BAR/AXI access.
 *============================================================================*/

/**
 * @brief Read from a NOC address.
 *
 * @param dev     Device ID
 * @param noc_id  NOC to use (0 or 1)
 * @param x       Core X coordinate
 * @param y       Core Y coordinate
 * @param addr    Address within core's memory
 * @param data    Buffer to receive data
 * @param size    Bytes to read
 * @return        TT_OK or error code
 */
tt_result_t tt_noc_read(tt_device_id_t dev,
                        uint8_t noc_id,
                        uint8_t x, uint8_t y,
                        uint64_t addr,
                        void *data, size_t size);

/**
 * @brief Write to a NOC address.
 */
tt_result_t tt_noc_write(tt_device_id_t dev,
                         uint8_t noc_id,
                         uint8_t x, uint8_t y,
                         uint64_t addr,
                         const void *data, size_t size);

/**
 * @brief Read 32-bit from NOC.
 */
tt_result_t tt_noc_read32(tt_device_id_t dev,
                          uint8_t noc_id,
                          uint8_t x, uint8_t y,
                          uint64_t addr,
                          uint32_t *value);

/**
 * @brief Write 32-bit to NOC.
 */
tt_result_t tt_noc_write32(tt_device_id_t dev,
                           uint8_t noc_id,
                           uint8_t x, uint8_t y,
                           uint64_t addr,
                           uint32_t value);

/**
 * @brief Broadcast write to all cores.
 */
tt_result_t tt_noc_broadcast(tt_device_id_t dev,
                             uint8_t noc_id,
                             uint64_t addr,
                             const void *data, size_t size);

/**
 * @brief Multicast write to rectangular core region.
 */
tt_result_t tt_noc_multicast(tt_device_id_t dev,
                             uint8_t noc_id,
                             uint8_t start_x, uint8_t start_y,
                             uint8_t end_x, uint8_t end_y,
                             uint64_t addr,
                             const void *data, size_t size);

/*============================================================================
 * 9. ARC MESSAGING
 *
 * ARC is the embedded controller managing firmware, power, and clocks.
 * These functions send raw messages - no policy about when/why to send.
 *============================================================================*/

/**
 * @brief ARC message result.
 */
typedef struct tt_arc_msg_result {
    uint32_t return_code;   /**< ARC return code */
    uint32_t arg;           /**< Return argument */
} tt_arc_msg_result_t;

/**
 * @brief Send a message to ARC.
 *
 * @param dev        Device ID
 * @param msg_code   Message code
 * @param arg0       First argument
 * @param arg1       Second argument
 * @param wait       Wait for completion
 * @param timeout_ms Timeout (0 = default 1000ms)
 * @param[out] result Result (may be NULL if wait=false)
 * @return           TT_OK or error code
 */
tt_result_t tt_arc_msg(tt_device_id_t dev,
                       uint16_t msg_code,
                       uint16_t arg0,
                       uint16_t arg1,
                       bool wait,
                       uint32_t timeout_ms,
                       tt_arc_msg_result_t *result);

/**
 * @brief Send extended ARC message with 8 arguments.
 *
 * @param dev        Device ID
 * @param msg_code   Message code
 * @param args       Array of 8 arguments
 * @param timeout_ms Timeout (0 = default 1000ms)
 * @param[out] result Array of 8 return values
 * @return           TT_OK or error code
 */
tt_result_t tt_arc_msg_extended(tt_device_id_t dev,
                                uint16_t msg_code,
                                const uint32_t args[8],
                                uint32_t timeout_ms,
                                uint32_t result[8]);

/*============================================================================
 * 10. TELEMETRY
 *
 * Raw telemetry data read from device registers.
 *============================================================================*/

/**
 * @brief Telemetry data.
 *
 * Units:
 *   - Temperatures: millidegrees Celsius
 *   - Voltages: millivolts
 *   - Power: milliwatts
 *   - Current: milliamps
 *   - Clocks: MHz
 */
typedef struct tt_telemetry {
    /* Identification */
    tt_arch_t arch;
    uint64_t board_id;
    uint16_t board_type;

    /* Firmware versions */
    uint32_t arc_fw_version;
    uint32_t arc1_fw_version;
    uint32_t eth_fw_version;
    uint32_t m3_bl_fw_version;
    uint32_t m3_app_fw_version;
    uint32_t fw_bundle_version;
    uint32_t ddr_fw_version;

    /* Temperatures (millidegrees C) */
    int32_t asic_temperature;
    int32_t vreg_temperature;
    int32_t board_temperature;
    int32_t outlet_temperature1;
    int32_t outlet_temperature2;
    int32_t gddr_temperature[8];    /**< Blackhole only */

    /* Power/voltage */
    uint32_t vcore;                 /**< Core voltage (mV) */
    uint32_t tdp;                   /**< Power (mW) */
    uint32_t tdc;                   /**< Current (mA) */
    uint32_t input_power;           /**< Input power (mW) */
    uint32_t vdd_min;               /**< Min voltage limit (mV) */
    uint32_t vdd_max;               /**< Max voltage limit (mV) */

    /* Clocks (MHz) */
    uint32_t aiclk;
    uint32_t axiclk;
    uint32_t arcclk;

    /* Status */
    uint32_t arc_health;            /**< Heartbeat counter */
    uint32_t ddr_status;            /**< Per-channel training bits */
    uint32_t ddr_speed;             /**< Speed grade */
    uint32_t eth_status;            /**< Link status bitmask */
    uint32_t pcie_status;
    uint32_t faults;
    uint32_t throttler;

    /* Thermal */
    uint32_t fan_speed;
    uint32_t therm_trip_limit;
    uint32_t therm_throttle_limit;

    /* Timing */
    uint32_t boot_date;
    uint32_t uptime_seconds;
} tt_telemetry_t;

/**
 * @brief Read telemetry from device.
 *
 * @param dev            Device ID
 * @param[out] telemetry Receives telemetry data
 * @return               TT_OK or error code
 */
tt_result_t tt_get_telemetry(tt_device_id_t dev, tt_telemetry_t *telemetry);

/*============================================================================
 * 11. SPI FLASH ACCESS
 *
 * Raw SPI flash operations. No policy about what/when to flash.
 * WARNING: Improper use can brick the device!
 *============================================================================*/

/**
 * @brief Read from SPI flash.
 */
tt_result_t tt_spi_read(tt_device_id_t dev,
                        uint32_t addr,
                        void *data,
                        size_t size);

/**
 * @brief Write to SPI flash.
 *
 * Caller must erase before writing.
 */
tt_result_t tt_spi_write(tt_device_id_t dev,
                         uint32_t addr,
                         const void *data,
                         size_t size);

/**
 * @brief Erase SPI sector.
 */
tt_result_t tt_spi_erase_sector(tt_device_id_t dev, uint32_t addr);

/**
 * @brief Get SPI flash parameters.
 */
tt_result_t tt_spi_get_info(tt_device_id_t dev,
                            uint32_t *page_size,
                            uint32_t *sector_size,
                            uint32_t *total_size);

/*============================================================================
 * 12. REMOTE CHIP ACCESS (Ethernet)
 *
 * Access chips connected via Ethernet in multi-chip systems.
 *============================================================================*/

/**
 * @brief Neighbor chip information.
 */
typedef struct tt_neighbor {
    tt_eth_addr_t eth_addr;
    tt_noc_coord_t local_port;
    tt_noc_coord_t remote_port;
    bool routing_enabled;
} tt_neighbor_t;

/**
 * @brief Get neighboring chips.
 *
 * @param dev            Device ID
 * @param[out] neighbors Array to receive neighbors
 * @param max_count      Maximum entries
 * @return               Number found, or negative error
 */
int tt_get_neighbors(tt_device_id_t dev,
                     tt_neighbor_t *neighbors,
                     size_t max_count);

/**
 * @brief Get local chip coordinates in mesh.
 *
 * @param dev       Device ID
 * @param[out] addr Receives chip's Ethernet address
 * @return          TT_OK or error code
 */
tt_result_t tt_get_local_coord(tt_device_id_t dev, tt_eth_addr_t *addr);

/**
 * @brief Read from remote chip via Ethernet.
 */
tt_result_t tt_eth_noc_read(tt_device_id_t dev,
                            tt_eth_addr_t remote,
                            uint8_t noc_id,
                            uint8_t x, uint8_t y,
                            uint64_t addr,
                            void *data, size_t size);

/**
 * @brief Write to remote chip via Ethernet.
 */
tt_result_t tt_eth_noc_write(tt_device_id_t dev,
                             tt_eth_addr_t remote,
                             uint8_t noc_id,
                             uint8_t x, uint8_t y,
                             uint64_t addr,
                             const void *data, size_t size);

/**
 * @brief Broadcast to remote chip via Ethernet.
 */
tt_result_t tt_eth_noc_broadcast(tt_device_id_t dev,
                                 tt_eth_addr_t remote,
                                 uint8_t noc_id,
                                 uint64_t addr,
                                 const void *data, size_t size);

/**
 * @brief Multicast to remote chip via Ethernet.
 */
tt_result_t tt_eth_noc_multicast(tt_device_id_t dev,
                                 tt_eth_addr_t remote,
                                 uint8_t noc_id,
                                 uint8_t start_x, uint8_t start_y,
                                 uint8_t end_x, uint8_t end_y,
                                 uint64_t addr,
                                 const void *data, size_t size);

/*============================================================================
 * 13. RESET OPERATIONS (Mechanism Only)
 *
 * Raw reset sequences. Higher layers decide when/why to reset.
 *============================================================================*/

/**
 * @brief Reset type.
 */
typedef enum tt_reset_type {
    TT_RESET_SOFT = 0,  /**< Firmware restart */
    TT_RESET_LINK = 1,  /**< PCIe link reset */
    TT_RESET_FULL = 2,  /**< Full chip reset */
} tt_reset_type_t;

/**
 * @brief Trigger device reset.
 *
 * This is a raw mechanism. After reset, the device may need
 * re-enumeration depending on reset type.
 */
tt_result_t tt_reset(tt_device_id_t dev, tt_reset_type_t type);

/**
 * @brief IPMI reset for Galaxy systems.
 */
tt_result_t tt_ipmi_reset(const char *ubb_num,
                          const char *dev_num,
                          const char *op_mode,
                          const char *reset_time);

/**
 * @brief Wait for driver to detect devices after reset.
 */
tt_result_t tt_wait_for_driver_load(uint32_t timeout_ms);

/*============================================================================
 * 14. BOOT FILESYSTEM (Blackhole)
 *============================================================================*/

/**
 * @brief Boot filesystem entry.
 */
typedef struct tt_boot_fs_entry {
    uint32_t spi_addr;
    uint32_t copy_dest;
    uint32_t image_size;
    uint32_t data_crc;
    uint32_t flags;
    char tag[8];
} tt_boot_fs_entry_t;

/**
 * @brief Decode boot filesystem entry by tag.
 *
 * Blackhole only.
 */
tt_result_t tt_decode_boot_fs_entry(tt_device_id_t dev,
                                    const char *tag,
                                    tt_boot_fs_entry_t *entry);

/*============================================================================
 * 15. ETHERNET CORE INFORMATION
 *============================================================================*/

/**
 * @brief Get Ethernet core locations.
 *
 * @param dev           Device ID
 * @param[out] cores    Array for coordinates
 * @param[out] enabled  Array for enabled status (may be NULL)
 * @param max_count     Maximum entries
 * @return              Number of cores, or negative error
 */
int tt_get_eth_cores(tt_device_id_t dev,
                     tt_noc_coord_t *cores,
                     bool *enabled,
                     size_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* TT_ACCESS_H */
