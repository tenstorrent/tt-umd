/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/remote_protocol.hpp"

#include "umd/device/tt_device/remote_communication.hpp"

namespace tt::umd {

RemoteProtocol::RemoteProtocol(
    std::unique_ptr<RemoteCommunication> remote_communication, architecture_implementation* architecture_impl) :
    remote_communication_(std::move(remote_communication)), architecture_impl_(architecture_impl) {}

void RemoteProtocol::write_to_device(const void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("RemoteProtocol::write_to_device not yet implemented");
}

void RemoteProtocol::read_from_device(void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("RemoteProtocol::read_from_device not yet implemented");
}

tt::ARCH RemoteProtocol::get_arch() { return architecture_impl_->get_architecture(); }

architecture_implementation* RemoteProtocol::get_architecture_implementation() { return architecture_impl_; }

int RemoteProtocol::get_communication_device_id() const {
    return remote_communication_->get_local_device()->get_communication_device_id();
}

IODeviceType RemoteProtocol::get_communication_device_type() {
    return remote_communication_->get_local_device()->get_communication_device_type();
}

void RemoteProtocol::detect_hang_read(uint32_t) {
    throw std::runtime_error("RemoteProtocol::detect_hang_read not yet implemented");
}

bool RemoteProtocol::is_hardware_hung() {
    throw std::runtime_error("RemoteProtocol::is_hardware_hung not yet implemented");
}

RemoteCommunication* RemoteProtocol::get_remote_communication() { return remote_communication_.get(); }

void RemoteProtocol::wait_for_non_mmio_flush() {
    throw std::runtime_error("RemoteProtocol::wait_for_non_mmio_flush not yet implemented");
}

}  // namespace tt::umd
