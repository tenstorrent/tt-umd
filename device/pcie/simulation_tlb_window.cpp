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

}  // namespace tt::umd
