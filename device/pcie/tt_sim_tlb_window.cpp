// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/tt_sim_tlb_window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "umd/device/chip_helpers/tt_sim_tlb_manager.hpp"
#include "umd/device/pcie/tt_sim_tlb_handle.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

TTSimTlbWindow::TTSimTlbWindow(
    std::unique_ptr<TlbHandle> handle, TTSimCommunicator* communicator, const tlb_data config) :
    TlbWindow(std::move(handle), config), sim_communicator_(communicator) {}

void TTSimTlbWindow::write16(uint64_t offset, uint16_t value) {
    validate(offset, sizeof(uint16_t));
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), &value, sizeof(uint16_t));
}

uint16_t TTSimTlbWindow::read16(uint64_t offset) {
    validate(offset, sizeof(uint16_t));
    uint16_t value = 0;
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), &value, sizeof(uint16_t));
    return value;
}

void TTSimTlbWindow::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), &value, sizeof(uint32_t));
}

uint32_t TTSimTlbWindow::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    uint32_t value = 0;
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), &value, sizeof(uint32_t));
    return value;
}

void TTSimTlbWindow::write_register(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

void TTSimTlbWindow::read_register(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

void TTSimTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

void TTSimTlbWindow::read_block(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

uint64_t TTSimTlbWindow::get_physical_address(uint64_t offset) const {
    // Get the base address from the TLB handle and add the offset
    // This should give us the physical address in the simulated device memory space.
    return reinterpret_cast<uint64_t>(tlb_handle->get_base()) + get_total_offset(offset);
}

void TTSimTlbWindow::safe_write16(uint64_t offset, uint16_t value) { write16(offset, value); }

uint16_t TTSimTlbWindow::safe_read16(uint64_t offset) { return read16(offset); }

void TTSimTlbWindow::safe_write32(uint64_t offset, uint32_t value) {
    // In simulation, we can assume all accesses are "safe" since we're not dealing with real hardware constraints.
    // However, we still want to validate the offset and size to prevent out-of-bounds access in our simulated memory.
    write32(offset, value);
}

uint32_t TTSimTlbWindow::safe_read32(uint64_t offset) {
    // Similar to safe_write32, we will just call the regular read32 after validating the offset.
    return read32(offset);
}

void TTSimTlbWindow::safe_write_register(uint64_t offset, const void* data, size_t size) {
    write_register(offset, data, size);
}

void TTSimTlbWindow::safe_read_register(uint64_t offset, void* data, size_t size) { read_register(offset, data, size); }

void TTSimTlbWindow::safe_write_block(uint64_t offset, const void* data, size_t size) {
    write_block(offset, data, size);
}

void TTSimTlbWindow::safe_read_block(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void TTSimTlbWindow::safe_write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    write_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void TTSimTlbWindow::safe_read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    read_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void TTSimTlbWindow::safe_noc_multicast_write_reconfigure(
    void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    noc_multicast_write_reconfigure(dst, size, core_start, core_end, addr, ordering);
}

tt::ARCH TTSimTlbWindow::get_arch() const {
    TTSimTlbHandle* sim_handle = dynamic_cast<TTSimTlbHandle*>(tlb_handle.get());
    return sim_handle->get_tlb_manager()->get_tt_device()->get_arch();
}

}  // namespace tt::umd
