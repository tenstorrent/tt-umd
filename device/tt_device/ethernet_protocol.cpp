/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/ethernet_protocol.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

EthernetProtocol::EthernetProtocol(
    std::unique_ptr<RemoteCommunication> remote_communication,
    eth_coord_t target_chip,
    architecture_implementation& architecture_implementation) :
    target_chip_(target_chip),
    remote_communication_(std::move(remote_communication)),
    architecture_implementation_(architecture_implementation) {
    // Assume local_tt_device in remote_communication is properly assigned
}

void EthernetProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->write_to_non_mmio(core, mem_ptr, addr, size);
}

void EthernetProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->read_non_mmio(core, mem_ptr, addr, size);
}

void EthernetProtocol::write_to_arc(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    write_to_device(
        mem_ptr, arc_core_, architecture_implementation_.get_arc_noc_apb_peripheral_offset() + arc_addr_offset, size);
}

void EthernetProtocol::read_from_arc(void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    read_from_device(
        mem_ptr, arc_core_, architecture_implementation_.get_arc_noc_apb_peripheral_offset() + arc_addr_offset, size);
}

void EthernetProtocol::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

bool EthernetProtocol::is_remote() { return true; }

}  // namespace tt::umd
