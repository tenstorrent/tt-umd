// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/simulation_tlb_window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "umd/device/simulation/tt_sim_communicator.hpp"

namespace tt::umd {

SimulationTlbWindow::SimulationTlbWindow(
    std::unique_ptr<TlbHandle> handle, TTSimCommunicator* communicator, const tlb_data config) :
    TlbWindow(std::move(handle), config), sim_communicator_(communicator) {}

void SimulationTlbWindow::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), &value, sizeof(uint32_t));
}

uint32_t SimulationTlbWindow::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    uint32_t value = 0;
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), &value, sizeof(uint32_t));
    return value;
}

void SimulationTlbWindow::write_register(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

void SimulationTlbWindow::read_register(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

void SimulationTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

void SimulationTlbWindow::read_block(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

uint64_t SimulationTlbWindow::get_physical_address(uint64_t offset) const {
    // Get the base address from the TLB handle and add the offset
    // This should give us the physical address in the simulated device memory space.
    return reinterpret_cast<uint64_t>(tlb_handle->get_base()) + get_total_offset(offset);
}

void SimulationTlbWindow::safe_write32(uint64_t offset, uint32_t value) {
    // In simulation, we can assume all accesses are "safe" since we're not dealing with real hardware constraints.
    // However, we still want to validate the offset and size to prevent out-of-bounds access in our simulated memory.
    write32(offset, value);
}

uint32_t SimulationTlbWindow::safe_read32(uint64_t offset) {
    // Similar to safe_write32, we will just call the regular read32 after validating the offset.
    return read32(offset);
}

void SimulationTlbWindow::safe_write_register(uint64_t offset, const void* data, size_t size) {
    write_register(offset, data, size);
}

void SimulationTlbWindow::safe_read_register(uint64_t offset, void* data, size_t size) {
    read_register(offset, data, size);
}

void SimulationTlbWindow::safe_write_block(uint64_t offset, const void* data, size_t size) {
    write_block(offset, data, size);
}

void SimulationTlbWindow::safe_read_block(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void SimulationTlbWindow::safe_write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    write_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void SimulationTlbWindow::safe_read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    read_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void SimulationTlbWindow::safe_noc_multicast_write_reconfigure(
    void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    noc_multicast_write_reconfigure(dst, size, core_start, core_end, addr, ordering);
}

}  // namespace tt::umd
