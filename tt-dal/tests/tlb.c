// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ttdal.h>

int main(void) {
    // Find a device
    tt_device_t devs[1];
    ssize_t count = tt_device_discover(1, devs);
    if (count <= 0)
        return 0;

    // Open device
    assert(tt_device_open(&devs[0]) == 0);

    // Test TLB allocation
    tt_tlb_t tlb;
    assert(tt_tlb_alloc(&devs[0], TT_TLB_2MB, TT_TLB_WC, &tlb) == 0);
    assert(tlb.ptr == NULL);
    assert(tlb.len == TT_TLB_2MB);

    // Test TLB configuration
    tt_tlb_config_t cfg = {
        .addr = 0,
        .x_start = 0,
        .y_start = 0,
        .x_end = 1,
        .y_end = 1,
        .noc = 0,
        .mcast = false,
        .linked = false,
        .static_vc = false,
    };
    assert(tt_tlb_configure(&devs[0], &tlb, &cfg) == 0);
    assert(tlb.ptr != NULL);

    // Test reconfiguration
    cfg.addr = 0x1000;
    void *old_ptr = tlb.ptr;
    assert(tt_tlb_configure(&devs[0], &tlb, &cfg) == 0);
    assert(tlb.ptr != NULL);
    assert(tlb.ptr != old_ptr);

    // Test TLB free
    assert(tt_tlb_free(&devs[0], &tlb) == 0);

    // Test allocating with unopened device
    tt_device_close(&devs[0]);
    assert(tt_tlb_alloc(&devs[0], TT_TLB_2MB, TT_TLB_WC, &tlb) < 0);
    assert(tt_errno == TT_ENOTOPEN);

    return 0;
}
