/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/jtag_protocol.hpp"

#include "umd/device/jtag/jtag_device.hpp"

namespace tt::umd {

JtagProtocol::JtagProtocol(
    std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id, architecture_implementation* architecture_impl) :
    jtag_device_(std::move(jtag_device)), communication_device_id_(jlink_id), architecture_impl_(architecture_impl) {}

void JtagProtocol::write_to_device(const void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("JtagProtocol::write_to_device not yet implemented");
}

void JtagProtocol::read_from_device(void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("JtagProtocol::read_from_device not yet implemented");
}

bool JtagProtocol::write_to_device_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t) { return false; }

tt::ARCH JtagProtocol::get_arch() { return architecture_impl_->get_architecture(); }

architecture_implementation* JtagProtocol::get_architecture_implementation() { return architecture_impl_; }

int JtagProtocol::get_communication_device_id() const { return communication_device_id_; }

IODeviceType JtagProtocol::get_communication_device_type() { return IODeviceType::JTAG; }

void JtagProtocol::detect_hang_read(uint32_t) {}

bool JtagProtocol::is_hardware_hung() { return false; }

JtagDevice* JtagProtocol::get_jtag_device() { return jtag_device_.get(); }

}  // namespace tt::umd
