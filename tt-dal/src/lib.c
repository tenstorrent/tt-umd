// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "ttdal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ioctl.h"

// Thread-local errno for thread safety
_Thread_local tt_result_t tt_errno = TT_OK;

/*============================================================================
 * DEVICE
 *============================================================================*/

/**
 * Discover connected devices.
 *
 * Scans `/dev/tenstorrent/` directory. Uses `d_type == DT_CHR` to
 * filter character devices.
 */
ssize_t tt_device_discover(size_t cap, tt_device_t buf[static cap]) {
    // Scan device directory
    DIR *dir = opendir("/dev/tenstorrent");
    if (!dir)
        return -(tt_errno = TT_ENODEV);

    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip non-character devices
        //
        // Only want `/dev/tenstorrent/N` devices.
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

/**
 * Open device file descriptor.
 *
 * Opens fd if not already open (NOP if `fd >= 0`). Uses `O_APPEND` to
 * signal power-aware client to kernel driver.
 */
tt_result_t tt_device_open(tt_device_t *dev) {
    if (!dev)
        return -(tt_errno = TT_EINVAL);

    // Check already open
    if (dev->fd >= 0)
        return TT_OK;

    // Open block device
    char path[64];
    snprintf(path, sizeof(path), "/dev/tenstorrent/%u", dev->id);
    dev->fd = open(path, O_RDWR | O_CLOEXEC | O_APPEND);
    if (dev->fd < 0)
        return -(tt_errno = TT_ENODEV);

    return TT_OK;
}

/**
 * Close device file descriptor.
 *
 * Closes fd if open (NOP if `fd < 0`). Sets `fd` to `-1` on success.
 */
tt_result_t tt_device_close(tt_device_t *dev) {
    if (!dev)
        return -(tt_errno = TT_EINVAL);

    // Already closed
    if (dev->fd < 0)
        return TT_OK;

    // Close device
    if (close(dev->fd) != 0)
        return -(tt_errno = TT_EIO);

    dev->fd = -1;
    return TT_OK;
}

/**
 * Get information about a device.
 *
 * Queries device info via `ioctl`. Copies output struct directly.
 */
tt_result_t tt_get_device_info(tt_device_t *dev, tt_device_info_t *info) {
    if (!dev || !info)
        return -(tt_errno = TT_EINVAL);

    // Ensure device is open
    if (dev->fd < 0)
        return -(tt_errno = TT_ENOTOPEN);

    // Query device info via `ioctl`
    struct tenstorrent_get_device_info query = {
        .in.output_size_bytes = sizeof(query.out),
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &query) != 0)
        return -(tt_errno = TT_EIO);

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

/*============================================================================
 * TLB
 *============================================================================*/

/**
 * Allocate a TLB window.
 *
 * Allocates TLB via `ioctl`. Does not `mmap` yet; `ptr` is `NULL`
 * until `tt_tlb_configure()` is called.
 */
tt_result_t tt_tlb_alloc(
    tt_device_t *dev,
    tt_tlb_size_t size,
    tt_tlb_cache_mode_t mode,
    tt_tlb_t *tlb
) {
    if (!dev || !tlb)
        return -(tt_errno = TT_EINVAL);

    // Ensure device is open
    if (dev->fd < 0)
        return -(tt_errno = TT_ENOTOPEN);

    // Allocate TLB via `ioctl`
    struct tenstorrent_allocate_tlb alloc = {
        .in.size = (size_t)size,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc) != 0)
        return -(tt_errno = TT_ENOMEM);

    // Calculate `mmap` offset based on cache mode
    uint64_t offset = (mode == TT_TLB_UC)
        ? alloc.out.mmap_offset_uc
        : alloc.out.mmap_offset_wc;

    // Populate TLB handle
    //
    // `ptr == NULL` until configured to prevent use-before-configure
    // bugs. Accessing `ptr` before `tt_tlb_configure()` will
    // `SIGSEGV`.
    *tlb = (tt_tlb_t){
        .id = alloc.out.id,
        .ptr = NULL,
        .len = (size_t)size,
        .offset = offset,
    };

    return TT_OK;
}

/**
 * Configure TLB mapping.
 *
 * `mmap`s TLB on first call. On reconfigure, `munmap`s old and
 * remaps new to invalidate stale interior pointers.
 */
tt_result_t tt_tlb_configure(
    tt_device_t *dev,
    tt_tlb_t *tlb,
    const tt_tlb_config_t *cfg
) {
    if (!dev || !tlb || !cfg)
        return -(tt_errno = TT_EINVAL);

    // Ensure device is open
    if (dev->fd < 0)
        return -(tt_errno = TT_ENOTOPEN);

    // Map TLB into user space
    //
    // On reconfigure, we `mmap` the new address BEFORE unmapping the
    // old one. This ensures kernel gives us a different virtual
    // address, invalidating any stale interior pointers users may
    // have saved (they'll `SIGSEGV` rather than silently accessing
    // different device memory).
    void *ptr = mmap(
        NULL,
        tlb->len,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        dev->fd,
        tlb->offset
    );
    if (ptr == MAP_FAILED) {
        // `mmap` failed
        //
        // On reconfigure failure, we leave the TLB in a completely
        // unconfigured state (`ptr = NULL`) rather than pointing to
        // stale configuration. This ensures users can't accidentally
        // use the old (wrong) mapping.
        if (tlb->ptr != NULL) {
            munmap(tlb->ptr, tlb->len);
            tlb->ptr = NULL;
        }
        return -(tt_errno = TT_ENOMEM);
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
        .in.config.ordering = cfg->ordering,
        .in.config.linked = cfg->linked,
        .in.config.static_vc = cfg->static_vc,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &mapping) != 0) {
        // `ioctl` failed
        //
        // `munmap` the new mapping we just created, then `munmap` the
        // old mapping (if reconfigure) to leave TLB completely
        // unconfigured. This prevents use of stale configuration.
        munmap(ptr, tlb->len);
        if (tlb->ptr != NULL) {
            munmap(tlb->ptr, tlb->len);
            tlb->ptr = NULL;
        }
        return -(tt_errno = TT_EINVAL);
    }

    // Unmap old mapping if this is a reconfigure
    if (tlb->ptr != NULL)
        munmap(tlb->ptr, tlb->len);

    // Update TLB with new mapping
    tlb->ptr = ptr;

    return TT_OK;
}

/**
 * Free a TLB window.
 *
 * `munmap`s TLB from user space (if mapped) then frees via `ioctl`.
 */
tt_result_t tt_tlb_free(
    tt_device_t *dev,
    const tt_tlb_t *tlb
) {
    if (!dev || !tlb)
        return -(tt_errno = TT_EINVAL);

    // Ensure device is open
    if (dev->fd < 0)
        return -(tt_errno = TT_ENOTOPEN);

    // Unmap TLB if it was configured
    //
    // If `tt_tlb_configure()` was never called, `ptr == NULL` and we
    // skip `munmap` (nothing to unmap).
    if (tlb->ptr != NULL && munmap(tlb->ptr, tlb->len) != 0)
        return -(tt_errno = TT_EINVAL);

    // Free TLB via `ioctl`
    struct tenstorrent_free_tlb free = {
        .in.id = tlb->id,
    };
    if (ioctl(dev->fd, TENSTORRENT_IOCTL_FREE_TLB, &free) != 0)
        return -(tt_errno = TT_EINVAL);

    return TT_OK;
}

/*============================================================================
 * MESSAGING
 *============================================================================*/

// Forward declarations
tt_result_t tt_wh_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
);
tt_result_t tt_bh_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
);

/**
 * Send ARC message to device.
 *
 * Dispatches to architecture-specific implementation based on device
 * architecture.
 */
tt_result_t tt_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
) {
    if (!dev || !msg)
        return -(tt_errno = TT_EINVAL);

    // Ensure device is open
    if (dev->fd < 0)
        return -(tt_errno = TT_ENOTOPEN);

    // Get device architecture
    tt_device_info_t info;
    tt_result_t res = tt_get_device_info(dev, &info);
    if (res != TT_OK)
        return res;

    // Dispatch to architecture-specific implementation
    switch (info.device_id) {
        case TT_ARCH_WORMHOLE:
            return tt_wh_arc_msg(dev, msg, wait, timeout);
        case TT_ARCH_BLACKHOLE:
            return tt_bh_arc_msg(dev, msg, wait, timeout);
        default:
            return -(tt_errno = TT_EBADARCH);
    }
}

/*============================================================================
 * TELEMETRY
 *============================================================================*/

// Forward declarations
tt_result_t tt_wh_get_telemetry(
    tt_device_t *dev,
    tt_telemetry_t table
);
tt_result_t tt_bh_get_telemetry(
    tt_device_t *dev,
    tt_telemetry_t table
);

/**
 * Get device telemetry.
 *
 * Dispatches to architecture-specific implementation based on device
 * architecture.
 */
tt_result_t tt_get_telemetry(
    tt_device_t *dev,
    tt_telemetry_t table
) {
    if (!dev || !table)
        return -(tt_errno = TT_EINVAL);

    // Ensure device is open
    if (dev->fd < 0)
        return -(tt_errno = TT_ENOTOPEN);

    // Get device architecture
    tt_device_info_t info;
    tt_result_t res = tt_get_device_info(dev, &info);
    if (res != TT_OK)
        return res;

    // Dispatch to architecture-specific implementation
    switch (info.device_id) {
        case TT_ARCH_WORMHOLE:
            return tt_wh_get_telemetry(dev, table);
        case TT_ARCH_BLACKHOLE:
            return tt_bh_get_telemetry(dev, table);
        default:
            return -(tt_errno = TT_EBADARCH);
    }
}

/*============================================================================
 * RESET
 *============================================================================*/

/**
 * Reset device.
 *
 * Opens dedicated `fd` for reset operation. Never relies on
 * `dev->fd` since it may be in bad state (reset should be
 * infallible).
 */
tt_result_t tt_reset(tt_device_t *dev) {
    if (!dev)
        return -(tt_errno = TT_EINVAL);

    // Close existing `fd` if open
    //
    // Reset invalidates all fds and TLBs, so we close the existing
    // `fd` to prevent stale state. Don't check return value, we want
    // reset to work even if `close` fails.
    tt_device_close(dev);

    // Open dedicated `fd` for reset
    //
    // Don't use `dev->fd`, open our own temporary `fd` for reset
    // operation to ensure it works even if `dev->fd` is corrupted.
    char path[64];
    snprintf(path, sizeof(path), "/dev/tenstorrent/%u", dev->id);
    int fd = open(path, O_RDWR | O_CLOEXEC | O_APPEND);
    if (fd < 0)
        return -(tt_errno = TT_ENODEV);

    // Issue reset `ioctl`
    struct tenstorrent_reset_device reset = {
        .in.output_size_bytes = sizeof(reset.out),
        .in.flags = TENSTORRENT_RESET_DEVICE_USER_RESET,
    };
    int ret = ioctl(fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset);

    // Close dedicated `fd`
    close(fd);

    if (ret != 0)
        return -(tt_errno = TT_EIO);

    return TT_OK;
}
