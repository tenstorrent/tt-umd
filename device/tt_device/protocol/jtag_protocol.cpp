/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/jtag_protocol.hpp"

#include <utility>

#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

JtagProtocol::JtagProtocol(std::unique_ptr<JtagDevice> jtag_device, uint8_t jlink_id) :
    jtag_device_(std::move(jtag_device)), mmio_id_(jlink_id) {}

JtagProtocol::~JtagProtocol() = default;

void JtagProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    jtag_device_->write(mmio_id_, mem_ptr, core.x, core.y, addr, size, static_cast<uint8_t>(noc_id));
}

void JtagProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    jtag_device_->read(mmio_id_, mem_ptr, core.x, core.y, addr, size, static_cast<uint8_t>(noc_id));
}

void JtagProtocol::write_to_device_reg(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    validate_register_access(addr, size);
    write_to_device(mem_ptr, core, addr, size, noc_id);
}

void JtagProtocol::read_from_device_reg(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    validate_register_access(addr, size);
    read_from_device(mem_ptr, core, addr, size, noc_id);
}

bool JtagProtocol::write_to_core_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t, NocId) { return false; }

int JtagProtocol::get_mmio_id() { return mmio_id_; }

JtagDevice* JtagProtocol::get_jtag_device() { return jtag_device_.get(); }

}  // namespace tt::umd
