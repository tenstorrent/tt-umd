/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/jtag_protocol.hpp"

namespace tt::umd {

void JtagProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device_->write(jlink_id_, mem_ptr, core.x, core.y, addr, size);
}

void JtagProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device_->read(jlink_id_, mem_ptr, core.x, core.y, addr, size);
}

void JtagProtocol::wait_for_non_mmio_flush() {}

bool JtagProtocol::is_remote() { return false; }

}  // namespace tt::umd
