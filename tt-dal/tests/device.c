// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // Test device discovery
    tt_device_t devs[16];
    ssize_t count = tt_device_discover(16, devs);
    assert(count >= 0);

    // If devices available, test open/close
    if (count > 0) {
        // Test open
        assert(tt_device_open(&devs[0]) == 0);
        assert(devs[0].fd >= 0);

        // Test double open (should be NOP)
        assert(tt_device_open(&devs[0]) == 0);
        assert(devs[0].fd >= 0);

        // Test get device info
        tt_device_info_t info;
        assert(tt_get_device_info(&devs[0], &info) == 0);
        assert(info.vendor_id != 0);

        // Test close
        assert(tt_device_close(&devs[0]) == 0);

        // Test double close (should be NOP)
        assert(tt_device_close(&devs[0]) == 0);
    }

    return 0;
}
