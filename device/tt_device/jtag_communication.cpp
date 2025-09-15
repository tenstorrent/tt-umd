// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/jtag_communication.hpp"

namespace tt::umd {

void JtagCommunication::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device->write(jlink_id, mem_ptr, core.x, core.y, addr, size);
}

void JtagCommunication::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device->read(jlink_id, mem_ptr, core.x, core.y, addr, size);
}

void JtagCommunication::wait_for_non_mmio_flush() {
    // Empty implementation
}

bool JtagCommunication::is_remote() { return false; }

}  // namespace tt::umd
