// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TTKMD_H_
#define TTKMD_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a Tenstorrent PCIe device.
 */
typedef struct tt_device_t tt_device_t;

/**
 * @brief Opaque handle to a TLB window.
 *
 * A TLB window is a fixed size aperture in the host address space that is
 * mappable to a device NOC (Network on Chip) location.
 */
typedef struct tt_tlb_t tt_tlb_t;

/**
 * @brief Opaque handle to a DMA mapping.
 *
 * A DMA mapping is host memory made device-accessible by the driver.
 */
typedef struct tt_dma_t tt_dma_t;

/**
 * @brief Configuration for a TLB window's mapping to the device NOC.
 *
 * These parameters control how memory operations on a TLB window are translated
 * into transactions on the device's NOC. See `tt_tlb_map()` for details.
 */
typedef struct tt_noc_addr_config_t {
    uint64_t addr;     /**< Local address aligned to the TLB window size */
    uint16_t x_end;    /**< X coord for unicast; rectangle end for multicast */
    uint16_t y_end;    /**< Y coord for unicast; rectangle end for multicast */
    uint16_t x_start;  /**< 0 for unicast; rectangle start for multicast */
    uint16_t y_start;  /**< 0 for unicast; rectangle start for multicast */
    uint8_t noc;       /**< 0 or 1 */
    uint8_t mcast;     /**< 1 to enable multicast */
    uint8_t ordering;  /**< Ordering semantics; see `enum tt_noc_ordering` */
    uint8_t static_vc; /**< 1 to enable static virtual channel */
} tt_noc_addr_config_t;

/**
 * @brief Raw device information returned from the kernel driver.
 */
typedef struct tt_device_info_t {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint16_t pci_domain;
    uint16_t bus_dev_fn; /**< [0:2] function, [3:7] device, [8:15] bus */
} tt_device_info_t;

/**
 * @brief BAR mapping entry returned by tt_query_mappings().
 */
typedef struct tt_bar_mapping_t {
    uint32_t mapping_id;   /**< One of enum tt_bar_mapping_id */
    uint64_t mapping_base; /**< Offset to use with mmap() on the device fd */
    uint64_t mapping_size; /**< Size of the mapping in bytes */
} tt_bar_mapping_t;

/**
 * @brief Supported Tenstorrent device architectures.
 */
enum tt_device_arch { TT_DEVICE_ARCH_UNKNOWN = 0, TT_DEVICE_ARCH_WORMHOLE, TT_DEVICE_ARCH_BLACKHOLE };

/**
 * @brief Queryable attributes of a Tenstorrent device.
 */
enum tt_device_attr {
    TT_DEVICE_ATTR_PCI_DOMAIN = 0,
    TT_DEVICE_ATTR_PCI_BUS = 1,
    TT_DEVICE_ATTR_PCI_DEVICE = 2,
    TT_DEVICE_ATTR_PCI_FUNCTION = 3,
    TT_DEVICE_ATTR_PCI_VENDOR_ID = 4,
    TT_DEVICE_ATTR_PCI_DEVICE_ID = 5,
    TT_DEVICE_ATTR_PCI_SUBSYSTEM_ID = 6,
    TT_DEVICE_ATTR_CHIP_ARCH = 7,
    TT_DEVICE_ATTR_NUM_1M_TLBS = 8,
    TT_DEVICE_ATTR_NUM_2M_TLBS = 9,
    TT_DEVICE_ATTR_NUM_16M_TLBS = 10,
    TT_DEVICE_ATTR_NUM_4G_TLBS = 11,
};

/**
 * @brief Queryable attributes of the Tenstorrent driver.
 */
enum tt_driver_attr {
    TT_DRIVER_API_VERSION = 0,
    TT_DRIVER_SEMVER_MAJOR = 1,
    TT_DRIVER_SEMVER_MINOR = 2,
    TT_DRIVER_SEMVER_PATCH = 3,
};

/**
 * @brief Caching modes for TLB windows mapped to the NOC.
 */
enum tt_tlb_cache_mode {
    TT_MMIO_CACHE_MODE_UC = 0, /**< Uncached; use for register accesses */
    TT_MMIO_CACHE_MODE_WC = 1, /**< Write Combined; use for memory accesses */
};

/**
 * @brief Ordering modes for NOC transactions.
 */
enum tt_noc_ordering {
    TT_NOC_ORDERING_RELAXED = 0,      /**< Relaxed (no read-after-write hazard) */
    TT_NOC_ORDERING_STRICT = 1,       /**< Full AXI ordering */
    TT_NOC_ORDERING_POSTED = 2,       /**< May have read-after-write hazard */
    TT_NOC_ORDERING_POSTED_STRICT = 3 /**< BH only, Unicast only */
};

/**
 * @brief Supported TLB window sizes.
 */
#define TT_TLB_SIZE_1M (1ULL << 20)  /**< 1 MiB TLB window (WH only) */
#define TT_TLB_SIZE_2M (1ULL << 21)  /**< 2 MiB TLB window (WH and BH) */
#define TT_TLB_SIZE_16M (1ULL << 24) /**< 16 MiB TLB window (WH only) */
#define TT_TLB_SIZE_4G (1ULL << 32)  /**< 4 GiB TLB window (BH only) */

/**
 * @brief BAR mapping IDs returned by tt_query_mappings().
 *
 * Resource0 = BAR0, Resource1 = BAR2, Resource2 = BAR4.
 */
enum tt_bar_mapping_id {
    TT_BAR_MAPPING_UNUSED = 0,
    TT_BAR_MAPPING_RESOURCE0_UC = 1,
    TT_BAR_MAPPING_RESOURCE0_WC = 2,
    TT_BAR_MAPPING_RESOURCE1_UC = 3,
    TT_BAR_MAPPING_RESOURCE1_WC = 4,
    TT_BAR_MAPPING_RESOURCE2_UC = 5,
    TT_BAR_MAPPING_RESOURCE2_WC = 6,
};

/**
 * @brief Flags controlling how pages are pinned / DMA-mapped.
 *
 * Used with `tt_dma_map()`, `tt_pin_pages()`, and `tt_pin_pages_noc()`.
 * `TT_DMA_FLAG_NOC` and `TT_DMA_FLAG_NOC_TOP_DOWN` are mutually exclusive.
 */
enum tt_dma_map_flags {
    TT_DMA_FLAG_NONE = 0,

    /**
     * @brief Requests a NOC-to-host aperture mapping, allocated bottom-up.
     */
    TT_DMA_FLAG_NOC = 1 << 0,

    /**
     * @brief Requests a NOC-to-host aperture mapping, allocated top-down.
     */
    TT_DMA_FLAG_NOC_TOP_DOWN = 1 << 1,

    /**
     * @brief Caller attests that the pages are physically contiguous.
     *
     * Required for the no-IOMMU hugepage and legacy DMA paths.
     */
    TT_DMA_FLAG_CONTIGUOUS = 1 << 2,
};

/**
 * @brief Power domain flags for tt_set_power_state().
 */
#define TT_POWER_FLAG_MAX_AI_CLK (1U << 0)       /**< 1=Max AI Clock, 0=Min AI Clock */
#define TT_POWER_FLAG_MRISC_PHY_WAKEUP (1U << 1) /**< 1=PHY Wakeup, 0=PHY Powerdown */
#define TT_POWER_FLAG_TENSIX_ENABLE (1U << 2)    /**< 1=Enable Tensix, 0=Clock Gate */
#define TT_POWER_FLAG_L2CPU_ENABLE (1U << 3)     /**< 1=Enable L2CPU, 0=Clock Gate */

/* -------------------------------------------------------------------------
 * Device lifecycle
 * ---------------------------------------------------------------------- */

/**
 * @brief Open a Tenstorrent device.
 *
 * @param chardev_path e.g. "/dev/tenstorrent/0"
 * @param out_dev Device handle
 * @param extra_flags Additional flags to pass to open(2), e.g. O_APPEND.
 */
int tt_device_open(const char* chardev_path, tt_device_t** out_dev, int extra_flags);

/**
 * @brief Close a Tenstorrent device.
 *
 * @param dev Device handle
 */
int tt_device_close(tt_device_t* dev);

/**
 * @brief Get the underlying file descriptor.
 *
 * The returned fd is owned by `dev` and must not be closed by the caller.
 * It may be used for mmap() operations on BAR regions.
 *
 * @param dev Device handle
 * @param out_fd The file descriptor
 */
int tt_device_get_fd(tt_device_t* dev, int* out_fd);

/**
 * @brief Get raw device information without a full tt_device_t handle.
 *
 * Useful for lightweight enumeration where only an fd is available.
 * The caller retains ownership of the fd.
 *
 * @param fd Open file descriptor for a tenstorrent chardev
 * @param out Device info
 */
int tt_device_get_info_fd(int fd, tt_device_info_t* out);

/**
 * @brief Get device information.
 *
 * @param dev Device handle
 * @param out Device info
 */
int tt_device_get_info(tt_device_t* dev, tt_device_info_t* out);

/**
 * @brief Query device attributes.
 *
 * @param dev Device handle
 * @param attr Attribute to query
 * @param out_value Attribute value
 * @return 0 on success, error code on failure
 */
int tt_device_get_attr(tt_device_t* dev, enum tt_device_attr attr, uint64_t* out_value);

/**
 * @brief Query driver attributes.
 *
 * @param dev Device handle; may be NULL for API version query
 * @param attr Attribute to query
 * @param out_value Attribute value
 * @return 0 on success, error code on failure
 */
int tt_driver_get_attr(tt_device_t* dev, enum tt_driver_attr attr, uint64_t* out_value);

/**
 * @brief Query BAR memory mappings available on this device.
 *
 * Fills `out_mappings` with up to `count` entries. The kernel fills slots
 * for all valid BARs; unused slots have mapping_id == TT_BAR_MAPPING_UNUSED.
 *
 * @param dev Device handle
 * @param out_mappings Caller-allocated array to receive mapping entries
 * @param count Number of slots in out_mappings (8 is sufficient)
 * @param out_count Set to the number of slots filled
 */
int tt_query_mappings(tt_device_t* dev, tt_bar_mapping_t* out_mappings, uint32_t count, uint32_t* out_count);

/**
 * @brief Reset the device.
 *
 * @param dev Device handle
 * @param flags Reset flags (values mirror TenstorrentResetDevice in pci_device.hpp)
 * @return 0 on success, negative errno on failure
 */
int tt_device_reset(tt_device_t* dev, uint32_t flags);

/**
 * @brief Set the power state for this device file descriptor.
 *
 * Requires KMD >= 2.6.0. Declares power requirements to the driver; the driver
 * aggregates across all open file descriptors. Passing power_flags=0 releases
 * all power requirements, allowing the device to enter low-power idle.
 *
 * @param dev Device handle (must have been opened with O_APPEND)
 * @param power_flags Bitmask of TT_POWER_FLAG_* values
 * @return 0 on success, negative errno on failure
 */
int tt_set_power_state(tt_device_t* dev, uint16_t power_flags);

/* -------------------------------------------------------------------------
 * NOC access (convenience functions)
 * ---------------------------------------------------------------------- */

/**
 * @brief Convenience function to read a 32-bit value from a device NOC address.
 *
 * @param dev Device handle
 * @param x NOC0 x-coordinate
 * @param y NOC0 y-coordinate
 * @param addr NOC address
 * @param value Pointer to store the read value
 * @return int 0 on success, error code on failure
 */
int tt_noc_read32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t* value);

/**
 * @brief Convenience function to write a 32-bit value to a device NOC address.
 *
 * @param dev Device handle
 * @param x NOC0 x-coordinate
 * @param y NOC0 y-coordinate
 * @param addr NOC address
 * @param value Value to write
 * @return int 0 on success, error code on failure
 */
int tt_noc_write32(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, uint32_t value);

/**
 * @brief Convenience function for reading from the device NOC.
 *
 * @param dev Device handle
 * @param x NOC0 x-coordinate
 * @param y NOC0 y-coordinate
 * @param addr NOC address
 * @param dst Pointer to store the read data
 * @param len Number of bytes to read
 * @return int 0 on success, error code on failure
 */
int tt_noc_read(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, void* dst, size_t len);

/**
 * @brief Convenience function for writing to the device NOC.
 *
 * @param dev Device handle
 * @param x NOC0 x-coordinate
 * @param y NOC0 y-coordinate
 * @param addr NOC address
 * @param src Pointer to the data to write
 * @param len Number of bytes to write
 * @return int 0 on success, error code on failure
 */
int tt_noc_write(tt_device_t* dev, uint8_t x, uint8_t y, uint64_t addr, const void* src, size_t len);

/* -------------------------------------------------------------------------
 * DMA — high-level lifecycle-managed API
 * ---------------------------------------------------------------------- */

/**
 * @brief Pins a host memory buffer and maps it for device access.
 *
 * Returns a handle that must be released with tt_dma_unmap().
 *
 * @param dev Device handle
 * @param addr Virtual address; must be page-aligned
 * @param len Number of bytes; must be a multiple of the page size
 * @param flags Bitmask of enum tt_dma_map_flags
 * @param out_dma On success, a handle for the pinned mapping
 * @return 0 on success, error code on failure
 */
int tt_dma_map(tt_device_t* dev, void* addr, size_t len, int flags, tt_dma_t** out_dma);

/**
 * @brief Unpins a previously mapped memory region.
 *
 * @param dev Device handle
 * @param dma DMA handle from `tt_dma_map()`
 * @return 0 on success, error code on failure
 */
int tt_dma_unmap(tt_device_t* dev, tt_dma_t* dma);

/**
 * @brief Gets the DMA address (IOVA or PA) for a mapped memory region.
 *
 * @param dma DMA handle from `tt_dma_map()`
 * @param out_dma_addr DMA address
 * @return 0 on success, error code on failure
 */
int tt_dma_get_dma_addr(tt_dma_t* dma, uint64_t* out_dma_addr);

/**
 * @brief Gets the NOC-accessible address for a mapped memory region.
 *
 * @param dma DMA handle from `tt_dma_map()`
 * @param out_noc_addr NOC address
 * @return 0 on success, error code on failure
 */
int tt_dma_get_noc_addr(tt_dma_t* dma, uint64_t* out_noc_addr);

/* -------------------------------------------------------------------------
 * DMA — low-level fire-and-forget pin/unpin
 *
 * Use these when you don't need a handle and the pinning lifetime matches
 * the fd lifetime (e.g. hugepage and legacy DMA paths in pci_device).
 * ---------------------------------------------------------------------- */

/**
 * @brief Pin pages for DMA; physical address returned directly.
 *
 * Pages remain pinned until tt_unpin_pages() or the fd is closed.
 *
 * @param dev Device handle
 * @param va Virtual address; must be page-aligned
 * @param len Number of bytes; must be a multiple of the page size
 * @param flags Bitmask of enum tt_dma_map_flags (NOC flags ignored here)
 * @param out_pa Physical address (or IOVA)
 * @return 0 on success, negative errno on failure
 */
int tt_pin_pages(tt_device_t* dev, uint64_t va, size_t len, int flags, uint64_t* out_pa);

/**
 * @brief Pin pages and allocate a NOC-to-host aperture mapping.
 *
 * @param dev Device handle
 * @param va Virtual address; must be page-aligned
 * @param len Number of bytes; must be a multiple of the page size
 * @param flags Bitmask of enum tt_dma_map_flags
 * @param out_noc_addr NOC address for device-side access
 * @param out_pa Physical address (or IOVA)
 * @return 0 on success, negative errno on failure
 */
int tt_pin_pages_noc(tt_device_t* dev, uint64_t va, size_t len, int flags, uint64_t* out_noc_addr, uint64_t* out_pa);

/**
 * @brief Unpin previously pinned pages.
 *
 * @param dev Device handle
 * @param va Original virtual address passed to tt_pin_pages() / tt_pin_pages_noc()
 * @param len Number of bytes
 * @return 0 on success, negative errno on failure
 */
int tt_unpin_pages(tt_device_t* dev, uint64_t va, size_t len);

/**
 * @brief Allocate a kernel DMA buffer (no-IOMMU path).
 *
 * The buffer is freed when the fd is closed. The caller must mmap() the
 * returned mmap_offset against the device fd to access the buffer.
 *
 * @param dev Device handle
 * @param size Requested size in bytes
 * @param buf_index Buffer slot index [0, TENSTORRENT_MAX_DMA_BUFS)
 * @param out_pa Physical address of the buffer
 * @param out_mmap_offset Offset to pass to mmap() on the device fd
 * @param out_size Actual allocated size in bytes
 * @return 0 on success, negative errno on failure
 */
int tt_allocate_dma_buf(
    tt_device_t* dev,
    uint32_t size,
    uint8_t buf_index,
    uint64_t* out_pa,
    uint64_t* out_mmap_offset,
    uint32_t* out_size);

/* -------------------------------------------------------------------------
 * TLB management
 * ---------------------------------------------------------------------- */

/**
 * @brief Allocates a TLB window.
 *
 * Quantities and sizes of TLB windows vary by device architecture:
 *
 * Wormhole:
 *   156x  1 MiB windows
 *    10x  2 MiB windows
 *    20x 16 MiB windows
 * Blackhole:
 *   202x  2 MiB windows
 *     8x  4 GiB windows
 *
 * @param dev Device handle
 * @param size 1, 2, or 16 MiB (WH); 2 MiB or 4 GiB (BH)
 * @param cache Caching attribute; see `enum tt_tlb_cache_mode`
 * @param out_tlb On success, a handle to the allocated TLB window
 * @return 0 on success, error code on failure
 */
int tt_tlb_alloc(tt_device_t* dev, size_t size, enum tt_tlb_cache_mode cache, tt_tlb_t** out_tlb);

/**
 * @brief Releases a TLB window.
 *
 * @param dev Device handle
 * @param tlb TLB window to release
 * @return 0 on success, error code on failure
 */
int tt_tlb_free(tt_device_t* dev, tt_tlb_t* tlb);

/**
 * @brief Get a pointer to the MMIO region of a TLB window.
 *
 * @param tlb TLB window handle from `tt_tlb_alloc()`
 * @param out_mmio Pointer to the MMIO region
 * @return 0 on success, error code on failure
 */
int tt_tlb_get_mmio(tt_tlb_t* tlb, void** out_mmio);

int tt_tlb_get_id(tt_tlb_t* tlb, uint32_t* out_id);

/**
 * @brief Maps a TLB window to a NOC endpoint.
 *
 * @param dev Device handle
 * @param tlb TLB window handle from `tt_tlb_alloc()`
 * @param config NOC address configuration
 * @return 0 on success, error code on failure
 */
int tt_tlb_map(tt_device_t* dev, tt_tlb_t* tlb, tt_noc_addr_config_t* config);

/**
 * @brief Maps a TLB window to a unicast NOC endpoint.
 *
 * @param dev Device handle
 * @param tlb TLB window handle from `tt_tlb_alloc()`
 * @param x NOC0 x-coordinate
 * @param y NOC0 y-coordinate
 * @param addr Address in the tile; must be a multiple of the TLB size
 * @return 0 on success, error code on failure
 */
int tt_tlb_map_unicast(tt_device_t* dev, tt_tlb_t* tlb, uint8_t x, uint8_t y, uint64_t addr);

#ifdef __cplusplus
}
#endif

#endif  // TTKMD_H_
