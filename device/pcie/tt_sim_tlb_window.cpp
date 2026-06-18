// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/tt_sim_tlb_window.hpp"

#include <functional>
#include <memory>
#include <utility>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/pcie/tt_sim_tlb_handle.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

TTSimTlbWindow::TTSimTlbWindow(
    std::unique_ptr<TlbHandle> handle, TTSimCommunicator* communicator, const tlb_data config) :
    TlbWindow(std::move(handle), config), sim_communicator_(communicator) {}

void TTSimTlbWindow::write16(uint64_t offset, uint16_t value, const std::function<bool()>& /*on_timeout*/) {
    validate(offset, sizeof(uint16_t));
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), &value, sizeof(uint16_t));
}

uint16_t TTSimTlbWindow::read16(uint64_t offset, const std::function<bool()>& /*on_timeout*/) {
    validate(offset, sizeof(uint16_t));
    uint16_t value = 0;
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), &value, sizeof(uint16_t));
    return value;
}

void TTSimTlbWindow::write32(uint64_t offset, uint32_t value, const std::function<bool()>& /*on_timeout*/) {
    validate(offset, sizeof(uint32_t));
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), &value, sizeof(uint32_t));
}

uint32_t TTSimTlbWindow::read32(uint64_t offset, const std::function<bool()>& /*on_timeout*/) {
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

void TTSimTlbWindow::write_block(
    uint64_t offset, const void* data, size_t size, const std::function<bool()>& /*on_timeout*/) {
    validate(offset, size);
    sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

void TTSimTlbWindow::read_block(uint64_t offset, void* data, size_t size, const std::function<bool()>& /*on_timeout*/) {
    validate(offset, size);
    sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
}

uint64_t TTSimTlbWindow::get_physical_address(uint64_t offset) const {
    // Get the base address from the TLB handle and add the offset
    // This should give us the physical address in the simulated device memory space.
    return reinterpret_cast<uint64_t>(tlb_handle->get_base()) + get_total_offset(offset);
}

void TTSimTlbWindow::safe_write16(uint64_t offset, uint16_t value, const std::function<bool()>& on_timeout) {
    write16(offset, value, on_timeout);
}

uint16_t TTSimTlbWindow::safe_read16(uint64_t offset, const std::function<bool()>& on_timeout) {
    return read16(offset, on_timeout);
}

}  // namespace tt::umd
