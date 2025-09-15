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

void EthernetCommunication::write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr) {
    throw std::runtime_error(
        "This interface will change. PCIe specific interface for intermediate changes. Will have to think about "
        "refactoring.");
};

void EthernetCommunication::read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr) {
    throw std::runtime_error(
        "This interface will change. PCIe specific interface for intermediate changes. Will have to think about "
        "refactoring.");
};

void EthernetCommunication::write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) {
    throw std::runtime_error(
        "This interface will change. PCIe specific interface for intermediate changes. Will have to think about "
        "refactoring.");
};

void EthernetCommunication::write_regs(uint32_t byte_addr, uint32_t word_len, const void* data) {
    throw std::runtime_error(
        "This interface will change. PCIe specific interface for intermediate changes. Will have to think about "
        "refactoring.");
};

void EthernetCommunication::read_regs(uint32_t byte_addr, uint32_t word_len, void* data) {
    throw std::runtime_error(
        "This interface will change. PCIe specific interface for intermediate changes. Will have to think about "
        "refactoring.");
};

void EthernetCommunication::wait_for_non_mmio_flush() { remote_communication->wait_for_non_mmio_flush(); }

RemoteCommunication* EthernetCommunication::get_remote_communication() { return remote_communication.get(); }

}  // namespace tt::umd
