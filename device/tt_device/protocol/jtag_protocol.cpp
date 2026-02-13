/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/jtag_protocol.hpp"

#include "assert.hpp"
#include "noc_access.hpp"

namespace tt::umd {

JtagProtocol::JtagProtocol(
    std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id, architecture_implementation* architecture_impl) :
    jtag_device_(std::move(jtag_device)), communication_device_id_(jlink_id), architecture_impl_(architecture_impl) {}

void JtagProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device_->write(communication_device_id_, mem_ptr, core.x, core.y, addr, size, is_selected_noc1() ? 1 : 0);
}

void JtagProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    jtag_device_->read(communication_device_id_, mem_ptr, core.x, core.y, addr, size, is_selected_noc1() ? 1 : 0);
}

tt::ARCH JtagProtocol::get_arch() {
    if (!jtag_device_->get_device_cnt()) {
        TT_THROW("No JTAG device present.");
    }
    return jtag_device_->get_jtag_arch(0);
}

int JtagProtocol::get_communication_device_id() const { return communication_device_id_; }

architecture_implementation* JtagProtocol::get_architecture_implementation() { return architecture_impl_; }

JtagDevice* JtagProtocol::get_jtag_device() { return jtag_device_.get(); }

}  // namespace tt::umd
