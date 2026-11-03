// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// clang-format off
// This file is copied from KMD, so we don't want clang formatting diff.

#ifndef TTDRIVER_IOCTL_H_INCLUDED
#define TTDRIVER_IOCTL_H_INCLUDED

#include <linux/types.h>
#include <linux/ioctl.h>

#define TENSTORRENT_DRIVER_VERSION 2

#define TENSTORRENT_IOCTL_MAGIC 0xFA

#define TENSTORRENT_IOCTL_GET_DEVICE_INFO	_IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_GET_HARVESTING	_IO(TENSTORRENT_IOCTL_MAGIC, 1)
#define TENSTORRENT_IOCTL_QUERY_MAPPINGS	_IO(TENSTORRENT_IOCTL_MAGIC, 2)
#define TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF	_IO(TENSTORRENT_IOCTL_MAGIC, 3)
#define TENSTORRENT_IOCTL_FREE_DMA_BUF		_IO(TENSTORRENT_IOCTL_MAGIC, 4)
#define TENSTORRENT_IOCTL_GET_DRIVER_INFO	_IO(TENSTORRENT_IOCTL_MAGIC, 5)
#define TENSTORRENT_IOCTL_RESET_DEVICE		_IO(TENSTORRENT_IOCTL_MAGIC, 6)
#define TENSTORRENT_IOCTL_PIN_PAGES		_IO(TENSTORRENT_IOCTL_MAGIC, 7)
#define TENSTORRENT_IOCTL_LOCK_CTL		_IO(TENSTORRENT_IOCTL_MAGIC, 8)
#define TENSTORRENT_IOCTL_MAP_PEER_BAR		_IO(TENSTORRENT_IOCTL_MAGIC, 9)
#define TENSTORRENT_IOCTL_UNPIN_PAGES		_IO(TENSTORRENT_IOCTL_MAGIC, 10)
#define TENSTORRENT_IOCTL_ALLOCATE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 11)
#define TENSTORRENT_IOCTL_FREE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 12)
#define TENSTORRENT_IOCTL_CONFIGURE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 13)
#define TENSTORRENT_IOCTL_SET_NOC_CLEANUP		_IO(TENSTORRENT_IOCTL_MAGIC, 14)
#define TENSTORRENT_IOCTL_SET_POWER_STATE		_IO(TENSTORRENT_IOCTL_MAGIC, 15)
#define TENSTORRENT_IOCTL_EXPORT_TLB_DMABUF	_IO(TENSTORRENT_IOCTL_MAGIC, 16)
#define TENSTORRENT_IOCTL_NOC_READ	_IO(TENSTORRENT_IOCTL_MAGIC, 18)
#define TENSTORRENT_IOCTL_NOC_WRITE	_IO(TENSTORRENT_IOCTL_MAGIC, 19)
#define TENSTORRENT_IOCTL_READ_EVENTS	_IO(TENSTORRENT_IOCTL_MAGIC, 20)

// For tenstorrent_mapping.mapping_id. These are not array indices.
#define TENSTORRENT_MAPPING_UNUSED		0
#define TENSTORRENT_MAPPING_RESOURCE0_UC	1
#define TENSTORRENT_MAPPING_RESOURCE0_WC	2
#define TENSTORRENT_MAPPING_RESOURCE1_UC	3
#define TENSTORRENT_MAPPING_RESOURCE1_WC	4
#define TENSTORRENT_MAPPING_RESOURCE2_UC	5
#define TENSTORRENT_MAPPING_RESOURCE2_WC	6

#define TENSTORRENT_MAX_DMA_BUFS	256
#define TENSTORRENT_MAX_INBOUND_TLBS	256

#define TENSTORRENT_RESOURCE_LOCK_COUNT 64

struct tenstorrent_get_device_info_in {
	__u32 output_size_bytes;
};

struct tenstorrent_get_device_info_out {
	__u32 output_size_bytes;
	__u16 vendor_id;
	__u16 device_id;
	__u16 subsystem_vendor_id;
	__u16 subsystem_id;
	__u16 bus_dev_fn;	// [0:2] function, [3:7] device, [8:15] bus
	__u16 max_dma_buf_size_log2;	// Since 1.0
	__u16 pci_domain;		// Since 1.23
};

struct tenstorrent_get_device_info {
	struct tenstorrent_get_device_info_in in;
	struct tenstorrent_get_device_info_out out;
};

struct tenstorrent_query_mappings_in {
	__u32 output_mapping_count;
	__u32 reserved;
};

struct tenstorrent_mapping {
	__u32 mapping_id;
	__u32 reserved;
	__u64 mapping_base;
	__u64 mapping_size;
};

struct tenstorrent_query_mappings_out {
	struct tenstorrent_mapping mappings[0];
};

struct tenstorrent_query_mappings {
	struct tenstorrent_query_mappings_in in;
	struct tenstorrent_query_mappings_out out;
};

// tenstorrent_allocate_dma_buf_in.flags
#define TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA 2

struct tenstorrent_allocate_dma_buf_in {
	__u32 requested_size;
	__u8  buf_index;	// [0,TENSTORRENT_MAX_DMA_BUFS)
	__u8  flags;
	__u8  reserved0[2];
	__u64 reserved1[2];
};

struct tenstorrent_allocate_dma_buf_out {
	__u64 physical_address;	// or IOVA
	__u64 mapping_offset;
	__u32 size;
	__u32 reserved0;
	__u64 noc_address;	// valid if TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA is set
	__u64 reserved1;
};

struct tenstorrent_allocate_dma_buf {
	struct tenstorrent_allocate_dma_buf_in in;
	struct tenstorrent_allocate_dma_buf_out out;
};

struct tenstorrent_free_dma_buf_in {
};

struct tenstorrent_free_dma_buf_out {
};

struct tenstorrent_free_dma_buf {
	struct tenstorrent_free_dma_buf_in in;
	struct tenstorrent_free_dma_buf_out out;
};

struct tenstorrent_get_driver_info_in {
	__u32 output_size_bytes;
};

struct tenstorrent_get_driver_info_out {
	__u32 output_size_bytes;
	__u32 driver_version;		// IOCTL API version
	__u8 driver_version_major;
	__u8 driver_version_minor;
	__u8 driver_version_patch;
	__u8 reserved0;
};

struct tenstorrent_get_driver_info {
	struct tenstorrent_get_driver_info_in in;
	struct tenstorrent_get_driver_info_out out;
};

// legacy tenstorrent_reset_device_in.flags
#define TENSTORRENT_RESET_DEVICE_RESTORE_STATE 0
#define TENSTORRENT_RESET_DEVICE_RESET_PCIE_LINK 1
#define TENSTORRENT_RESET_DEVICE_CONFIG_WRITE 2

// tenstorrent_reset_device_in.flags
#define TENSTORRENT_RESET_DEVICE_USER_RESET 3
#define TENSTORRENT_RESET_DEVICE_ASIC_RESET 4
#define TENSTORRENT_RESET_DEVICE_ASIC_DMC_RESET 5
#define TENSTORRENT_RESET_DEVICE_POST_RESET 6

struct tenstorrent_reset_device_in {
	__u32 output_size_bytes;
	__u32 flags;
};

struct tenstorrent_reset_device_out {
	__u32 output_size_bytes;
	__u32 result;
};

struct tenstorrent_reset_device {
	struct tenstorrent_reset_device_in in;
	struct tenstorrent_reset_device_out out;
};

// tenstorrent_pin_pages_in.flags
#define TENSTORRENT_PIN_PAGES_CONTIGUOUS 1	// app attests that the pages are physically contiguous
#define TENSTORRENT_PIN_PAGES_NOC_DMA 2		// app wants to use the pages for NOC DMA
#define TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN 4	// NOC DMA will be allocated top-down (default is bottom-up)
#define TENSTORRENT_PIN_PAGES_READ_ONLY 8	// device will only read; requires IOMMU translation

struct tenstorrent_pin_pages_in {
	__u32 output_size_bytes;
	__u32 flags;
	__u64 virtual_address;
	__u64 size;
};

struct tenstorrent_pin_pages_out {
	__u64 physical_address;	// or IOVA
};

struct tenstorrent_pin_pages_out_extended {
	__u64 physical_address;	// or IOVA
	__u64 noc_address;
};

// unpinning subset of a pinned buffer is not supported
struct tenstorrent_unpin_pages_in {
	__u64 virtual_address;	// original VA used to pin, not current VA if remapped
	__u64 size;
	__u64 reserved;
};

struct tenstorrent_unpin_pages_out {
};

struct tenstorrent_unpin_pages {
	struct tenstorrent_unpin_pages_in in;
	struct tenstorrent_unpin_pages_out out;
};

struct tenstorrent_pin_pages {
	struct tenstorrent_pin_pages_in in;
	struct tenstorrent_pin_pages_out out;
};

// tenstorrent_lock_ctl_in.flags
#define TENSTORRENT_LOCK_CTL_ACQUIRE 0
#define TENSTORRENT_LOCK_CTL_RELEASE 1
#define TENSTORRENT_LOCK_CTL_TEST 2

struct tenstorrent_lock_ctl_in {
	__u32 output_size_bytes;
	__u32 flags;
	__u8  index;
};

struct tenstorrent_lock_ctl_out {
	__u8 value;
};

struct tenstorrent_lock_ctl {
	struct tenstorrent_lock_ctl_in in;
	struct tenstorrent_lock_ctl_out out;
};

struct tenstorrent_map_peer_bar_in {
	__u32 peer_fd;
	__u32 peer_bar_index;
	__u32 peer_bar_offset;
	__u32 peer_bar_length;
	__u32 flags;
};

struct tenstorrent_map_peer_bar_out {
	__u64 dma_address;
	__u64 reserved;
};

struct tenstorrent_map_peer_bar {
	struct tenstorrent_map_peer_bar_in in;
	struct tenstorrent_map_peer_bar_out out;
};

struct tenstorrent_allocate_tlb_in {
	__u64 size;
	__u64 reserved;
};

struct tenstorrent_allocate_tlb_out {
	__u32 id;
	__u32 reserved0;
	__u64 mmap_offset_uc;
	__u64 mmap_offset_wc;
	__u64 reserved1;
};

struct tenstorrent_allocate_tlb {
	struct tenstorrent_allocate_tlb_in in;
	struct tenstorrent_allocate_tlb_out out;
};

struct tenstorrent_free_tlb_in {
	__u32 id;
};

struct tenstorrent_free_tlb_out {
};

struct tenstorrent_free_tlb {
	struct tenstorrent_free_tlb_in in;
	struct tenstorrent_free_tlb_out out;
};

struct tenstorrent_noc_tlb_config {
	__u64 addr;
	__u16 x_end;
	__u16 y_end;
	__u16 x_start;
	__u16 y_start;
	__u8 noc;
	__u8 mcast;
	__u8 ordering;
	__u8 linked;
	__u8 static_vc;
	__u8 reserved0[3];
	__u32 reserved1[2];
};

struct tenstorrent_configure_tlb_in {
	__u32 id;
	struct tenstorrent_noc_tlb_config config;
};

struct tenstorrent_configure_tlb_out {
	__u64 reserved;
};

struct tenstorrent_configure_tlb {
	struct tenstorrent_configure_tlb_in in;
	struct tenstorrent_configure_tlb_out out;
};

/**
 * TENSTORRENT_IOCTL_SET_NOC_CLEANUP - Register a cleanup action
 *
 * Registers an automatic NOC write operation that the driver will perform on
 * the device when the file descriptor is closed. This provides a reliable
 * cleanup mechanism for device-side software in case the host-side userspace
 * application terminates abnormally (e.g. segfault, OOM killer).
 *
 * A previously registered action can be cleared by setting @enabled to 0.
 *
 * @argsz: Must be sizeof(struct tenstorrent_set_noc_cleanup).
 * @flags: Reserved for future use, must be 0.
 * @enabled: Set to 1 to register the action, or 0 to clear it.
 * @x: X coordinate of the NOC tile to write to.
 * @y: Y coordinate of the NOC tile to write to.
 * @noc: NOC ID to write to; must be 0 or 1.
 * @addr: NOC address to write to; must be 4-byte aligned.
 * @data: Data to write to the NOC tile; upper 32 bits are ignored.
 */
struct tenstorrent_set_noc_cleanup {
	__u32 argsz;
	__u32 flags;
	__u8 enabled;
	__u8 x;
	__u8 y;
	__u8 noc;
	__u32 reserved0;
	__u64 addr;
	__u64 data;
};

/**
 * TENSTORRENT_IOCTL_SET_POWER_STATE - Set per-client power requirements
 *
 * Declares the power requirements of this file descriptor to the driver.
 * The driver aggregates requirements across all open file descriptors,
 * applying bitwise OR for power_flags and maximum for power_settings.
 * When the file descriptor is closed, its contribution is removed.
 *
 * Opening the device with O_APPEND opts out of legacy mode, allowing
 * the device to enter low-power idle states when no client holds power flags.
 *
 * @argsz: Must be sizeof(struct tenstorrent_power_state).
 * @flags: Reserved for future use, must be 0.
 * @validity: Encodes how many flags and settings fields are valid.
 * @power_flags: Bitmask of power domain requests.
 * @power_settings: Array of numeric power settings.
 */
struct tenstorrent_power_state {
	__u32 argsz;
	__u32 flags;
	__u8 reserved0;
	__u8 validity;
#define TT_POWER_VALIDITY_FLAGS(n)    (((n) & 0xF) << 0)
#define TT_POWER_VALIDITY_SETTINGS(n) (((n) & 0xF) << 4)
#define TT_POWER_VALIDITY(flags_count, settings_count) \
	(TT_POWER_VALIDITY_FLAGS(flags_count) | TT_POWER_VALIDITY_SETTINGS(settings_count))
	__u16 power_flags;
#define TT_POWER_FLAG_MAX_AI_CLK       (1U << 0) /* 1=Max AI Clock,  0=Min AI Clock */
#define TT_POWER_FLAG_MRISC_PHY_WAKEUP (1U << 1) /* 1=PHY Wakeup,    0=PHY Powerdown */
#define TT_POWER_FLAG_TENSIX_ENABLE    (1U << 2) /* 1=Enable Tensix, 0=Clock Gate Tensix */
#define TT_POWER_FLAG_L2CPU_ENABLE     (1U << 3) /* 1=Enable L2CPU,  0=Clock Gate L2CPU */
	__u16 power_settings[14];
};

/**
 * TENSTORRENT_IOCTL_EXPORT_TLB_DMABUF - Export a TLB window as a dma-buf
 *
 * Wraps an already-allocated, already-configured TLB window in a dma-buf and
 * returns a file descriptor for it. That fd can be handed to another
 * device's driver (e.g. an RDMA NIC via ibv_reg_dmabuf_mr()) so the peer can
 * perform peer-to-peer PCIe DMA directly into and out of the window. Traffic
 * routes onto the NOC according to the window's CONFIGURE_TLB setup, with no
 * Tenstorrent-CPU involvement on the data path.
 *
 * The export must be issued on the same file descriptor that allocated the
 * window. offset and size must be page-aligned and lie within the window;
 * size of 0 means to the end of the window.
 *
 * The fd is the unit of ownership and is self-sufficient. An export pins the
 * window: FREE_TLB or close() of the owning fd does not return the window to
 * the allocation pool while the export is live, so the window cannot be
 * reallocated and reconfigured to redirect a live importer's DMA elsewhere on
 * the NOC. The window is freed only once the dma-buf is released. This lets
 * the export survive FREE_TLB, close() of the owning fd, and even device
 * removal.
 *
 * Resetting the device under in-flight P2P DMA can wedge the host hard enough
 * to require out-of-band recovery, and a pin-only importer cannot be revoked
 * to prevent the in-flight DMA. RESET_DEVICE is therefore refused with
 * -EBUSY while any export is live.
 *
 * This ioctl requires Linux 5.8+.
 *
 * @argsz: Must be sizeof(struct tenstorrent_export_tlb_dmabuf).
 * @flags: Reserved for future use, must be 0.
 * @tlb_id: A TLB window id returned by TENSTORRENT_IOCTL_ALLOCATE_TLB.
 * @fd: OUT: the dma-buf file descriptor.
 * @offset: Byte offset within the window at which the export begins; must be
 *          page-aligned.
 * @size: Number of bytes to export; 0 means to the end of the window. A
 *        non-zero size must be a multiple of the page size.
 */
struct tenstorrent_export_tlb_dmabuf {
	__u32 argsz;
	__u32 flags;
	__u32 tlb_id;
	__s32 fd;
	__u64 offset;
	__u64 size;
};


/**
 * struct tenstorrent_noc_io - TENSTORRENT_IOCTL_NOC_READ / _NOC_WRITE
 *
 * Scalar read or write of one naturally aligned location, performed by the
 * kernel through a window it owns rather than through a TLB the caller mapped.
 *
 * On Quasar @addr is a flat 52-bit system physical address and @x, @y and @noc
 * are unused and must be 0. Setting TENSTORRENT_NOC_FLAG_KLA instead
 * interprets @addr as a Quasar-local address, reached through a SYSIN0
 * aperture. Wormhole and Blackhole reject that flag.
 *
 * @argsz: Must be sizeof(struct tenstorrent_noc_io).
 * @flags: Zero or TENSTORRENT_NOC_FLAG_KLA.
 * @x: X coordinate of the NOC endpoint; must be in the range 0-63.
 * @y: Y coordinate of the NOC endpoint; must be in the range 0-63.
 * @noc: NOC ID to use; must be 0 or 1.
 * @width: Access width in bytes; must be 1, 2, 4 or 8.
 * @reserved0: Must be zero.
 * @addr: Address to access; must be naturally aligned to @width.
 * @value: For NOC_READ, receives the value (zero-extended to 64 bits). For
 *         NOC_WRITE, the value to write (only the low @width bytes are used).
 */
struct tenstorrent_noc_io {
	__u32 argsz;
	__u32 flags;
#define TENSTORRENT_NOC_FLAG_KLA	(1 << 0)
	__u16 x;
	__u16 y;
	__u8 noc;
	__u8 width;
	__u8 reserved0[2];
	__u64 addr;
	__u64 value;
};

// tenstorrent_event.type.  Types 0x0000-0x7FFF are defined by firmware (in
// tt-system-firmware's device_events.h) and passed through without
// interpretation.  The driver's own types are 0x8000 and up.
#define TENSTORRENT_EVENT_TYPE_FW_EVENTS_LOST		0x8000	// data[0]: firmware records lost

/**
 * struct tenstorrent_event - One device event record
 *
 * @seq: Driver sequence number.  Consecutive per device, starting at 0 when
 *       the device is probed.
 * @host_time_ns: CLOCK_MONOTONIC when the driver received the record.
 * @device_time: Firmware timestamp: the ARC reference clock since firmware
 *               boot, in 20 ns ticks.  0 for driver types.
 * @type: A firmware type, or TENSTORRENT_EVENT_TYPE_*.
 * @source: For an event raised by a tile, its translated NOC coordinates,
 *          x | y << 6.  Otherwise type-specific.
 * @cause: Type-specific subcode.
 * @data: Type-specific data.  Firmware records use data[0..2]; data[3] is 0.
 */
struct tenstorrent_event {
	__u64 seq;
	__u64 host_time_ns;
	__u64 device_time;
	__u16 type;
	__u16 source;
	__u32 cause;
	__u32 data[4];
};

// tenstorrent_read_events.flags
#define TENSTORRENT_READ_EVENTS_FROM_OLDEST	(1 << 0)

// tenstorrent_read_events.status
#define TENSTORRENT_READ_EVENTS_STATUS_FW_ATTACHED	(1 << 0)	// Firmware publishes events

/**
 * TENSTORRENT_IOCTL_READ_EVENTS - Read device event records
 *
 * The driver keeps the last 256 event records for each device, numbered by
 * tenstorrent_event.seq.  Records come from firmware and from the driver
 * itself, including firmware records published before the driver found the
 * device, such as errors at boot.  A device reset discards them all.
 *
 * Each fd has a cursor: the seq of the next record it will read.  It starts
 * at open() just past the newest record, so an fd sees only records that
 * arrive after it was opened.  READ_EVENTS copies records from the cursor
 * onwards, up to @capacity of them, and advances the cursor past them.  If
 * records the fd had not read were discarded to make room, the cursor first
 * moves to the oldest record kept, and @lost says how many were skipped.
 * Reading does not consume records: every fd has its own cursor.
 *
 * poll() on the fd reports EPOLLIN | EPOLLPRI while the fd has unread
 * records, and EPOLLERR | EPOLLHUP once the fd is no longer usable (device
 * removed, or reset after the fd was opened).
 *
 * If TENSTORRENT_READ_EVENTS_STATUS_FW_ATTACHED is clear in @status, firmware
 * does not publish events (it is too old, or the device is not ready), and no
 * records will appear.  A call with @capacity 0 reads nothing and
 * changes nothing, so it can be used to check this.
 *
 * @argsz: Must be sizeof(struct tenstorrent_read_events).
 * @flags: TENSTORRENT_READ_EVENTS_FROM_OLDEST moves the cursor to the oldest
 *         record kept before reading.
 * @entry_size: OUT: sizeof(struct tenstorrent_event).
 * @capacity: Number of records that fit at @events_ptr.
 * @count: OUT: number of records written to @events_ptr.
 * @lost: OUT: records this fd skipped since its previous read, saturating
 *        at 2^32 - 1.
 * @status: OUT: TENSTORRENT_READ_EVENTS_STATUS_* bits.
 * @reserved: Must be 0.
 * @events_ptr: User address of an array of @capacity struct tenstorrent_event.
 */
struct tenstorrent_read_events {
	__u32 argsz;
	__u32 flags;
	__u32 entry_size;
	__u32 capacity;
	__u32 count;
	__u32 lost;
	__u32 status;
	__u32 reserved;
	__u64 events_ptr;
};

#endif
// clang-format on
