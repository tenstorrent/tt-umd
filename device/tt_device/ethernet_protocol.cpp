/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/ethernet_protocol.hpp"

namespace tt::umd {

EthernetProtocol::EthernetProtocol(std::unique_ptr<RemoteCommunication> remote_communication, eth_coord_t target_chip) :
    target_chip_(target_chip), remote_communication_(std::move(remote_communication)) {
    // Assume local_tt_device in remote_communication is properly assigned
}

void EthernetProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->write_to_non_mmio(core, mem_ptr, addr, size);
}

void EthernetProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->read_non_mmio(core, mem_ptr, addr, size);
}

void EthernetProtocol::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

bool EthernetProtocol::is_remote() { return true; }

}  // namespace tt::umd
