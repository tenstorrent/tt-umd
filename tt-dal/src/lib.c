// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "ttdal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ioctl.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Thread-local errno for thread safety
_Thread_local tt_error_t tt_errno = TT_OK;

// Version constants
const unsigned int tt_version_major = TT_VERSION_MAJOR;
const unsigned int tt_version_minor = TT_VERSION_MINOR;
const unsigned int tt_version_patch = TT_VERSION_PATCH;

/*============================================================================*
 * DEVICE                                                                     *
 *============================================================================*/

/// Create device from path.
///
/// Parses device number from path and initializes device handle. The path must
/// be a device node under `/dev/tenstorrent/` (e.g., `/dev/tenstorrent/0` or
/// `/dev/tenstorrent/by-id/<board-id>`).
int tt_device_new(const char *path, tt_device_t *dev) {
    if (!path || !dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Resolve symlinks to get canonical path
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        return tt_errno = TT_ENODEV, TT_ERR;

    // Extract device number from path
    //
    // Expected format: /dev/tenstorrent/<number>
    const char *prefix = "/dev/tenstorrent/";
    size_t prefix_len = strlen(prefix);

    if (strncmp(resolved, prefix, prefix_len) != 0)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Parse device number
    const char *num_str = resolved + prefix_len;
    char *endptr;
    unsigned long num = strtoul(num_str, &endptr, 10);

    // Verify we parsed the entire remaining string (no trailing chars)
    if (*endptr != '\0' || num > UINT32_MAX)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Initialize device handle
    *dev = (tt_device_t){
        .id = (uint32_t)num,
        .fd = -1,
    };

    return TT_OK;
}

/// Discover connected devices.
///
/// Scans `/dev/tenstorrent/` directory for character devices.
ssize_t tt_device_discover(size_t cap, tt_device_t buf[static cap]) {
    // Scan device directory
    DIR *dir = opendir("/dev/tenstorrent");
    if (!dir)
        return tt_errno = TT_ENODEV, TT_ERR;

    // Iterate through directory
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip non-character devices
        if (entry->d_type != DT_CHR)
            continue;

        // Parse device number
        char *endptr;
        uint32_t num = (uint32_t)strtoul(entry->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue;

        // Store device record
        if (count < cap)
            buf[count] = (tt_device_t){.id = num, .fd = -1};
        count++;
    }

    closedir(dir);
    return (ssize_t)count;
}

/// Open device file descriptor.
///
/// Opens `fd` if not already open. NOP if `fd >= 0`.
///
/// Uses `O_APPEND` to signal power-aware client to kernel driver.
int tt_device_open(tt_device_t *dev) {
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Check already open
    if (dev->fd >= 0)
        return TT_OK;

    // Define path buffer
    char path[PATH_MAX];
    size_t len = snprintf(path, sizeof(path), "/dev/tenstorrent/%u", dev->id);
    if (len < 0 || len >= sizeof(path))
        return tt_errno = TT_ENOBUFS, TT_ERR; // BUG: internal error

    // Open block device
    dev->fd = open(path, O_RDWR | O_CLOEXEC | O_APPEND);
    if (dev->fd < 0)
        return tt_errno = TT_ENODEV, TT_ERR;

    return TT_OK;
}

/// Close device file descriptor.
///
/// Closes `fd` if open. NOP if `fd < 0`. Sets `fd` to `-1` on success.
int tt_device_close(tt_device_t *dev) {
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Check already closed
    if (dev->fd < 0)
        return TT_OK;

    // Close block device
    if (close(dev->fd) != 0)
        return tt_errno = TT_EIO, TT_ERR;

    // Clear file
    dev->fd = -1;

    return TT_OK;
}

/// Get information about a device.
///
/// Queries device info via `ioctl`. Copies output struct directly.
int tt_get_device_info(tt_device_t *dev, tt_device_info_t *info) {
    if (!dev || !info)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Query device info
    struct tenstorrent_get_device_info query = {
        .in.output_size_bytes = sizeof(query.out),
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &query) != 0)
        return tt_errno = TT_EIO, TT_ERR;

    // Copy device info from `ioctl`
    *info = (tt_device_info_t){
        .output_size_bytes = query.out.output_size_bytes,
        .vendor_id = query.out.vendor_id,
        .device_id = query.out.device_id,
        .subsystem_vendor_id = query.out.subsystem_vendor_id,
        .subsystem_id = query.out.subsystem_id,
        .bus_dev_fn = query.out.bus_dev_fn,
        .max_dma_buf_size_log2 = query.out.max_dma_buf_size_log2,
        .pci_domain = query.out.pci_domain,
    };

    return TT_OK;
}

/*============================================================================*
 * ADDRESSING                                                                 *
 *============================================================================*/

/// Allocate a TLB window.
///
/// Allocates TLB via `ioctl`. Does not `mmap` yet; `ptr == NULL` until
/// `tt_tlb_configure()` is called.
int tt_tlb_alloc(
    tt_device_t *dev,
    tt_tlb_size_t size,
    tt_tlb_cache_mode_t mode,
    tt_tlb_t *tlb
) {
    if (!dev || !tlb)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Allocate TLB
    struct tenstorrent_allocate_tlb alloc = {
        .in.size = (size_t)size,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc) != 0)
        return tt_errno = TT_ENOMEM, TT_ERR;

    // Populate TLB
    *tlb = (tt_tlb_t){
        .id = alloc.out.id,
        .ptr = NULL, // `NULL` until configured
        .len = (size_t)size,
    };

    // Determine offset
    //
    // The offset for `mmap` depends on the specified cache mode.
    uint64_t offset;
    switch (mode) {
        case TT_TLB_UC:
            tlb->idx = alloc.out.mmap_offset_uc;
            break;
        case TT_TLB_WC:
            tlb->idx = alloc.out.mmap_offset_wc;
            break;
        default:
            // Invalid cache mode
            tt_errno = TT_EINVAL;
            goto cleanup;
    }

    return TT_OK;

cleanup:
    // Free the newly allocated TLB
    tt_tlb_free(dev, tlb);

failure:
    return TT_ERR;
}

/// Configure TLB mapping.
///
/// `mmap`s TLB on first call. On reconfigure, `munmap`s old and remaps new to
/// invalidate stale interior pointers.
int tt_tlb_configure(
    tt_device_t *dev,
    tt_tlb_t *tlb,
    const tt_tlb_config_t *cfg
) {
    if (!dev || !tlb || !cfg)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Map TLB into user space
    //
    // On reconfigure, we `mmap` the new address before unmapping the old one.
    // This ensures kernel gives us a different virtual address, invalidating
    // any stale interior pointers users may have saved. (Traps with `SIGSEGV`
    // rather than silently accessing different device memory).
    void *ptr = mmap(
        NULL,
        tlb->len,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        dev->fd,
        tlb->idx
    );
    if (ptr == MAP_FAILED) {
        // `mmap` failed
        tt_errno = TT_ENOMEM;
        // Clean up the previous mapping
        goto cleanup;
    }

    // Configure TLB via `ioctl`
    struct tenstorrent_configure_tlb mapping = {
        .in.id = tlb->id,
        .in.config.addr = cfg->addr,
        .in.config.x_end = cfg->x_end,
        .in.config.y_end = cfg->y_end,
        .in.config.x_start = cfg->x_start,
        .in.config.y_start = cfg->y_start,
        .in.config.noc = cfg->noc,
        .in.config.mcast = cfg->mcast,
        .in.config.ordering = 1,  // strict
        .in.config.linked = cfg->linked,
        .in.config.static_vc = cfg->static_vc,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &mapping) != 0) {
        // `ioctl` failed
        tt_errno = TT_EINVAL;
        // Unmap the new mapping we just created before cleaning up the old
        // mapping (if reconfigure).
        munmap(ptr, tlb->len);
        goto cleanup;
    }

    // Unmap old mapping if this is a reconfigure
    if (tlb->ptr != NULL)
        munmap(tlb->ptr, tlb->len);

    // Update TLB with new mapping
    tlb->ptr = ptr;

    return TT_OK;

cleanup:
    // Unmap previous mapping, leaving the TLB completely unconfigured, to
    // ensure users can't accidentally use the (now) invalid mapping.
    if (tlb->ptr != NULL) {
        munmap(tlb->ptr, tlb->len);
        tlb->ptr = NULL;
    }

failure:
    return TT_ERR;
}

/// Free a TLB window.
///
/// `munmap`s TLB from user space (if mapped) then frees via `ioctl`.
int tt_tlb_free(
    tt_device_t *dev,
    tt_tlb_t *tlb
) {
    if (!dev || !tlb)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Unmap TLB
    //
    // Skip if unconfigured (nothing to unmap).
    if (tlb->ptr != NULL && munmap(tlb->ptr, tlb->len) != 0)
        return tt_errno = TT_EINVAL, TT_ERR;
    tlb->ptr = NULL;

    // Free TLB
    struct tenstorrent_free_tlb free = {
        .in.id = tlb->id,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_FREE_TLB, &free) != 0)
        return tt_errno = TT_EINVAL, TT_ERR;
    tlb->id = 0;

    return TT_OK;
}

/*============================================================================*
 * MESSAGING                                                                  *
 *============================================================================*/

// Forward declarations
int tt_wh_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
);
int tt_bh_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
);

/// Send ARC message to device.
///
/// Dispatches to architecture-specific implementation based on device
/// architecture.
int tt_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
) {
    if (!dev || !msg)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Get device architecture
    tt_device_info_t info;
    int res = tt_get_device_info(dev, &info);
    if (res < 0)
        return res;

    // Dispatch to architecture-specific implementation
    switch (info.device_id) {
        case TT_ARCH_WORMHOLE:
            return tt_wh_arc_msg(dev, msg, wait, timeout);
        case TT_ARCH_BLACKHOLE:
            return tt_bh_arc_msg(dev, msg, wait, timeout);
        default:
            return tt_errno = TT_EBADARCH, TT_ERR;
    }
}

/*============================================================================*
 * TELEMETRY                                                                  *
 *============================================================================*/

// Forward declarations
int tt_wh_get_telemetry(
    tt_device_t *dev,
    tt_telemetry_t table
);
int tt_bh_get_telemetry(
    tt_device_t *dev,
    tt_telemetry_t table
);

/// Get device telemetry.
///
/// Dispatches to architecture-specific implementation based on device
/// architecture.
int tt_get_telemetry(
    tt_device_t *dev,
    tt_telemetry_t table
) {
    if (!dev || !table)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Ensure device is open
    if (dev->fd < 0)
        return tt_errno = TT_ENOTOPEN, TT_ERR;

    // Get device architecture
    tt_device_info_t info;
    int res = tt_get_device_info(dev, &info);
    if (res < 0)
        return res;

    // Dispatch to architecture-specific implementation
    switch (info.device_id) {
        case TT_ARCH_WORMHOLE:
            return tt_wh_get_telemetry(dev, table);
        case TT_ARCH_BLACKHOLE:
            return tt_bh_get_telemetry(dev, table);
        default:
            return tt_errno = TT_EBADARCH, TT_ERR;
    }
}

/*============================================================================*
 * RESET                                                                      *
 *============================================================================*/

/// Reset device.
///
/// Reset should be infallible. Always reopens the device `fd` for the reset
/// operation, since it may be in bad state.
int tt_reset(tt_device_t *dev) {
    if (!dev)
        return tt_errno = TT_EINVAL, TT_ERR;

    // Close device if open
    //
    // Reset invalidates all fds and TLBs, so we close the existing `fd` to
    // prevent stale state. Don't check return value, we want reset to work even
    // if `close` fails.
    tt_device_close(dev);

    // Reopen device for reset operation
    //
    // Use `tt_device_open()` to get a fresh fd for the reset.
    if (tt_device_open(dev) < 0)
        return TT_ERR;

    // Issue reset `ioctl`
    struct tenstorrent_reset_device reset = {
        .in.output_size_bytes = sizeof(reset.out),
        .in.flags = TENSTORRENT_RESET_DEVICE_USER_RESET,
    };
    bool failed = false;
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset) != 0) {
        tt_device_close(dev);
        tt_errno = TT_EIO;
        failed = true;
        goto cleanup;
    }

cleanup:
    // Close device after reset
    //
    // Reset invalidates the fd, so close it to keep state clean.
    tt_device_close(dev);
    // Reset invalidates the fd, so close it to keep state clean.
    tt_device_close(dev);

    return TT_OK;
}
