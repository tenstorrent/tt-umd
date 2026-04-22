/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/remote_protocol.hpp"

#include "umd/device/tt_device/remote_communication.hpp"

namespace tt::umd {

RemoteProtocol::RemoteProtocol(std::unique_ptr<RemoteCommunication> remote_communication) :
    remote_communication_(std::move(remote_communication)) {}

void RemoteProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    remote_communication_->write_to_non_mmio(core, mem_ptr, addr, size);
}

void RemoteProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) {
    remote_communication_->read_non_mmio(core, mem_ptr, addr, size);
}

bool RemoteProtocol::write_to_core_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t) { return false; }

int RemoteProtocol::get_mmio_id() { return remote_communication_->get_local_device()->get_communication_device_id(); }

RemoteCommunication* RemoteProtocol::get_remote_communication() { return remote_communication_.get(); }

void RemoteProtocol::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

}  // namespace tt::umd
