/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/jtag_protocol.hpp"

#include <stdexcept>

#include "umd/device/jtag/jtag_device.hpp"

namespace tt::umd {

JtagProtocol::JtagProtocol(std::shared_ptr<JtagDevice> jtag_device, [[maybe_unused]] uint8_t jlink_id) :
    jtag_device_(std::move(jtag_device)) {}

void JtagProtocol::write_to_device(const void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("JtagProtocol::write_to_device not yet implemented");
}

void JtagProtocol::read_from_device(void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("JtagProtocol::read_from_device not yet implemented");
}

bool JtagProtocol::write_to_device_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t) { return false; }

JtagDevice* JtagProtocol::get_jtag_device() { return jtag_device_.get(); }

}  // namespace tt::umd
