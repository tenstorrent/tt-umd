/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/remote_protocol.hpp"

#include "umd/device/tt_device/remote_communication.hpp"

namespace tt::umd {

RemoteProtocol::RemoteProtocol(std::unique_ptr<RemoteCommunication> remote_communication) :
    remote_communication_(std::move(remote_communication)) {}

void RemoteProtocol::write_to_device(const void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("RemoteProtocol::write_to_device not yet implemented");
}

void RemoteProtocol::read_from_device(void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("RemoteProtocol::read_from_device not yet implemented");
}

bool RemoteProtocol::write_to_device_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t) { return false; }

RemoteCommunication* RemoteProtocol::get_remote_communication() { return remote_communication_.get(); }

void RemoteProtocol::wait_for_non_mmio_flush() {
    throw std::runtime_error("RemoteProtocol::wait_for_non_mmio_flush not yet implemented");
}

}  // namespace tt::umd
