/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/jtag_protocol.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

void JtagProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device_->write(jlink_id_, mem_ptr, core.x, core.y, addr, size, umd_use_noc1 ? 1 : 0);
}

void JtagProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device_->read(jlink_id_, mem_ptr, core.x, core.y, addr, size, umd_use_noc1 ? 1 : 0);
}

void JtagProtocol::write_to_arc(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    auto arc_noc_core = arc_core_;
    jtag_device_->write(
        jlink_id_,
        mem_ptr,
        arc_noc_core.x,
        arc_noc_core.y,
        architecture_implementation_.get_arc_noc_apb_peripheral_offset() + arc_addr_offset,
        sizeof(uint32_t));
    return;
}

void JtagProtocol::read_from_arc(void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    auto arc_noc_core = arc_core_;
    jtag_device_->read(
        jlink_id_,
        mem_ptr,
        arc_noc_core.x,
        arc_noc_core.y,
        architecture_implementation_.get_arc_noc_apb_peripheral_offset() + arc_addr_offset,
        sizeof(uint32_t));
    return;
}

void JtagProtocol::wait_for_non_mmio_flush() {}

bool JtagProtocol::is_remote() { return false; }

}  // namespace tt::umd
