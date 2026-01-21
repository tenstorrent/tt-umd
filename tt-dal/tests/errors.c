// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // Test NULL pointer handling
    assert(tt_device_open(NULL) < 0);
    assert(tt_errno == TT_EINVAL);

    assert(tt_device_close(NULL) < 0);
    assert(tt_errno == TT_EINVAL);

    // Test unopened device
    tt_device_t dev = {.id = 0, .fd = -1};
    tt_device_info_t info;
    assert(tt_get_device_info(&dev, &info) < 0);
    assert(tt_errno == TT_ENOTOPEN);

    // Test NULL info pointer
    tt_device_t devs[1];
    ssize_t count = tt_device_discover(1, devs);
    if (count > 0) {
        assert(tt_device_open(&devs[0]) == 0);
        assert(tt_get_device_info(&devs[0], NULL) < 0);
        assert(tt_errno == TT_EINVAL);
        tt_device_close(&devs[0]);
    }

    // Test TLB errors with NULL
    tt_tlb_t tlb;
    assert(tt_tlb_alloc(NULL, TT_TLB_2MB, TT_TLB_WC, &tlb) < 0);
    assert(tt_errno == TT_EINVAL);

    assert(tt_tlb_configure(NULL, &tlb, NULL) < 0);
    assert(tt_errno == TT_EINVAL);

    assert(tt_tlb_free(NULL, &tlb) < 0);
    assert(tt_errno == TT_EINVAL);

    // Test arc_msg errors
    tt_arc_msg_t msg = {0};
    assert(tt_arc_msg(NULL, &msg, false, 0) < 0);
    assert(tt_errno == TT_EINVAL);

    assert(tt_arc_msg(&dev, NULL, false, 0) < 0);
    assert(tt_errno == TT_EINVAL);

    // Test telemetry errors
    tt_telemetry_t table;
    assert(tt_get_telemetry(NULL, table) < 0);
    assert(tt_errno == TT_EINVAL);

    assert(tt_get_telemetry(&dev, NULL) < 0);
    assert(tt_errno == TT_EINVAL);

    // Test reset errors
    assert(tt_reset(NULL) < 0);
    assert(tt_errno == TT_EINVAL);

    return 0;
}
