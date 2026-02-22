// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "ttdal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

//==============================================================================
// BLACKHOLE ARCHITECTURE-SPECIFIC IMPLEMENTATION
//==============================================================================

/**
 * Send ARC message to Blackhole device.
 *
 * Allocates a TLB to the ARC CSM region, writes the message to mailbox
 * registers, and optionally polls for completion.
 */
int tt_bh_arc_msg(
    tt_device_t *dev,
    tt_arc_msg_t *msg,
    bool wait,
    uint32_t timeout
) {
    // TODO: Implement Blackhole ARC messaging
    // 1. Allocate TLB to ARC_CSM region
    // 2. Write message code and data to mailbox registers
    // 3. If wait, poll status register until completion or timeout
    // 4. Read response from mailbox
    // 5. Free TLB
    (void)dev;
    (void)msg;
    (void)wait;
    (void)timeout;
    return tt_errno = TT_ENOTSUP, -1;
}

/**
 * Get telemetry from Blackhole device.
 *
 * Allocates a TLB to the ARC scratch region and reads the telemetry table.
 */
int tt_bh_get_telemetry(
    tt_device_t *dev,
    tt_telemetry_t table
) {
    // TODO: Implement Blackhole telemetry reading
    // 1. Allocate TLB to ARC scratch region
    // 2. Read telemetry table from scratch memory
    // 3. Parse tags and populate telemetry struct
    // 4. Free TLB
    (void)dev;
    (void)table;
    return tt_errno = TT_ENOTSUP, -1;
}
