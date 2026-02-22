// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Example: Discover and list all Tenstorrent devices

#include <stdio.h>
#include <ttdal.h>

#define DEVBUFSZ 256

int main(void) {
    tt_device_t devs[DEVBUFSZ];
    ssize_t count = tt_device_discover(DEVBUFSZ, devs);

    if (count < 0) {
        fprintf(stderr, "Failed to discover devices: %s\n",
                tt_error_describe(tt_errno));
        return 1;
    }

    if (count == 0) {
        printf("No Tenstorrent devices found.\n");
        return 0;
    }

    printf("Found %zd device%s:\n\n", count, count == 1 ? "" : "s");

    size_t actual = (size_t)count < DEVBUFSZ ? (size_t)count : DEVBUFSZ;

    // Print table header
    printf("ID   Architecture   Vendor   Device   PCI Location       Max DMA\n");
    printf("--   ------------   ------   ------   ------------       -------\n");

    for (size_t i = 0; i < actual; i++) {
        tt_device_t *dev = &devs[i];

        if (tt_device_open(dev) < 0) {
            fprintf(stderr, "%-4u (failed to open: %s)\n", dev->id,
                    tt_error_describe(tt_errno));
            continue;
        }

        tt_device_info_t info = { 0 };
        info.output_size_bytes = sizeof(info);

        if (tt_get_device_info(dev, &info) < 0) {
            fprintf(stderr, "%-4u (failed to get info: %s)\n", dev->id,
                    tt_error_describe(tt_errno));
            tt_device_close(dev);
            continue;
        }

        tt_arch_t arch = (tt_arch_t)info.device_id;
        const char *arch_name = tt_arch_describe(arch);
        if (!arch_name) {
            arch_name = "Unknown";
        }

        uint8_t bus = (info.bus_dev_fn >> 8) & 0xFF;
        uint8_t device = (info.bus_dev_fn >> 3) & 0x1F;
        uint8_t function = info.bus_dev_fn & 0x07;

        size_t dma_size = 1UL << info.max_dma_buf_size_log2;
        const char *dma_unit = "B";
        if (dma_size >= 1024 * 1024 * 1024) {
            dma_size /= 1024 * 1024 * 1024;
            dma_unit = "GB";
        } else if (dma_size >= 1024 * 1024) {
            dma_size /= 1024 * 1024;
            dma_unit = "MB";
        } else if (dma_size >= 1024) {
            dma_size /= 1024;
            dma_unit = "KB";
        }

        printf("%-4u %-12s   0x%04x   0x%04x   %04x:%02x:%02x.%x   %zu %s\n",
               dev->id, arch_name, info.vendor_id, info.device_id,
               info.pci_domain, bus, device, function, dma_size, dma_unit);

        tt_device_close(dev);
    }

    if ((size_t)count > DEVBUFSZ) {
        printf("\nNote: %zd additional device%s not shown\n",
               count - DEVBUFSZ, (count - DEVBUFSZ) == 1 ? "" : "s");
    }

    return 0;
}
