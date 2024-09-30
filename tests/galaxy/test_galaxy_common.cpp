// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "test_galaxy_common.h"
void move_data(
    LocalChip& device, tt_multichip_core_addr sender_core, tt_multichip_core_addr receiver_core, uint32_t size) {
    std::vector<uint32_t> readback_vec = {};
    device.read_from_device(
        readback_vec, cxy_pair(sender_core.chip, sender_core.core), sender_core.addr, size, "SMALL_READ_WRITE_TLB");
    device.write_to_device(
        readback_vec, cxy_pair(receiver_core.chip, receiver_core.core), receiver_core.addr, "SMALL_READ_WRITE_TLB");
    device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

    return;
}

void broadcast_data(
    LocalChip& device,
    tt_multichip_core_addr sender_core,
    std::vector<tt_multichip_core_addr> receiver_cores,
    uint32_t size) {
    std::vector<uint32_t> readback_vec = {};
    device.read_from_device(
        readback_vec, cxy_pair(sender_core.chip, sender_core.core), sender_core.addr, size, "SMALL_READ_WRITE_TLB");
    for (const auto& receiver_core : receiver_cores) {
        device.write_to_device(
            readback_vec,
            cxy_pair(receiver_core.chip, receiver_core.core),
            receiver_core.addr,
            "SMALL_READ_WRITE_TLB");
    }
    device.wait_for_non_mmio_flush();  // Barrier to ensure that all writes over ethernet were commited

    return;
}
