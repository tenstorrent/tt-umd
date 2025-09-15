// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/ethernet_communication.hpp"

namespace tt::umd {

void EthernetCommunication::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication->write_to_non_mmio(target_chip, core, mem_ptr, addr, size);
}

void EthernetCommunication::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication->read_non_mmio(target_chip, core, mem_ptr, addr, size);
}

void EthernetCommunication::wait_for_non_mmio_flush() { remote_communication->wait_for_non_mmio_flush(); }

RemoteCommunication* EthernetCommunication::get_remote_communication() { return remote_communication.get(); }

}  // namespace tt::umd
