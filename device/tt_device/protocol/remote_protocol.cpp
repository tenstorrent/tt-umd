/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/remote_protocol.hpp"

namespace tt::umd {

RemoteProtocol::RemoteProtocol(
    std::unique_ptr<RemoteCommunication> remote_communication, architecture_implementation* architecture_impl) :
    remote_communication_(std::move(remote_communication)), architecture_impl_(architecture_impl) {}

void RemoteProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->write_to_non_mmio(core, mem_ptr, addr, size);
}

void RemoteProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->read_non_mmio(core, mem_ptr, addr, size);
}

tt::ARCH RemoteProtocol::get_arch() { return architecture_impl_->get_architecture(); }

int RemoteProtocol::get_communication_device_id() const {
    return remote_communication_->get_device_protocol()->get_communication_device_id();
}

IODeviceType RemoteProtocol::get_communication_device_type() {
    return remote_communication_->get_device_protocol()->get_communication_device_type();
}

architecture_implementation* RemoteProtocol::get_architecture_implementation() { return architecture_impl_; }

void RemoteProtocol::detect_hang_read(uint32_t data_read) {
    remote_communication_->get_device_protocol()->detect_hang_read();
}

bool RemoteProtocol::is_hardware_hung() { return remote_communication_->get_device_protocol()->is_hardware_hung(); }

RemoteCommunication* RemoteProtocol::get_remote_communication() { return remote_communication_.get(); }

void RemoteProtocol::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

}  // namespace tt::umd
