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
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

TTSimTlbWindow::TTSimTlbWindow(
    std::unique_ptr<TlbHandle> handle, TTSimCommunicator* communicator, const tlb_data config) :
    TlbWindow(std::move(handle), config), sim_communicator_(communicator) {}

void TTSimTlbWindow::write16(uint64_t offset, uint16_t value) { write_block(offset, &value, sizeof(value)); }

uint16_t TTSimTlbWindow::read16(uint64_t offset) {
    uint16_t value = 0;
    read_block(offset, &value, sizeof(value));
    return value;
}

void TTSimTlbWindow::write32(uint64_t offset, uint32_t value) { write_block(offset, &value, sizeof(value)); }

uint32_t TTSimTlbWindow::read32(uint64_t offset) {
    uint32_t value = 0;
    read_block(offset, &value, sizeof(value));
    return value;
}

void TTSimTlbWindow::write_register(uint64_t offset, const void* data, size_t size) { write_block(offset, data, size); }

void TTSimTlbWindow::read_register(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void TTSimTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    // Quasar's TLB handle programs no TLB registers and is left without a base address, so a
    // physical address derived from it carries no identity for the core this window was created
    // for. Name the core explicitly. Wormhole and Blackhole keep the physical-address path.
    //
    // This is an arch check rather than a capability check because the simulator does not model
    // Quasar TLBs at all. If it ever does, this needs to become a capability check.
    if (tlb_handle->get_arch() != tt::ARCH::QUASAR) {
        sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
        return;
    }
    // configure() stored local_offset in units of the TLB size, so scale it back to a device address.
    const tlb_data& config = tlb_handle->get_config();
    const uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    sim_communicator_->tile_write_bytes(
        static_cast<uint32_t>(config.x_end),
        static_cast<uint32_t>(config.y_end),
        device_addr,
        data,
        static_cast<uint32_t>(size));
}

void TTSimTlbWindow::read_block(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    if (tlb_handle->get_arch() != tt::ARCH::QUASAR) {
        sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
        return;
    }
    const tlb_data& config = tlb_handle->get_config();
    const uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    sim_communicator_->tile_read_bytes(
        static_cast<uint32_t>(config.x_end),
        static_cast<uint32_t>(config.y_end),
        device_addr,
        data,
        static_cast<uint32_t>(size));
}

uint64_t TTSimTlbWindow::get_physical_address(uint64_t offset) const {
    // Get the base address from the TLB handle and add the offset
    // This should give us the physical address in the simulated device memory space.
    return reinterpret_cast<uint64_t>(tlb_handle->get_base()) + get_total_offset(offset);
}

void TTSimTlbWindow::safe_write16(uint64_t offset, uint16_t value) { write16(offset, value); }

uint16_t TTSimTlbWindow::safe_read16(uint64_t offset) { return read16(offset); }

}  // namespace tt::umd
