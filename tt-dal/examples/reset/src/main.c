// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Example: Reset a Tenstorrent device

#include <stdio.h>
#include <stdlib.h>
#include <ttdal.h>

int main(int argc, char *argv[]) {
    // Parse command line arguments
    const char *path = "/dev/tenstorrent/0";  // Default

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [device-path]\n", argv[0]);
        fprintf(stderr, "  device-path: Path to device (default: /dev/tenstorrent/0)\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s /dev/tenstorrent/0\n", argv[0]);
        fprintf(stderr, "  %s /dev/tenstorrent/by-id/<board-id>\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        path = argv[1];
    }

    // Initialize device from path
    tt_device_t dev;
    if (tt_device_new(path, &dev) < 0) {
        fprintf(stderr, "Error: Failed to initialize device from path '%s': %s\n",
                path, tt_error_describe(tt_errno));
        return 1;
    }

    printf("Resetting device %u (from path: %s)...\n", dev.id, path);

    // Reset the device
    if (tt_reset(&dev) < 0) {
        fprintf(stderr, "Error: Failed to reset device: %s\n",
                tt_error_describe(tt_errno));
        return 1;
    }

    printf("Device reset successfully.\n");

    return 0;
}
