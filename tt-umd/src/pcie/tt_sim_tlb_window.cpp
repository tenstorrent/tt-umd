// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/tt_sim_tlb_window.hpp"

#include <functional>
#include <memory>
#include <utility>

#include "pcie/tlb_window_common.hpp"
#include "pcie/tt_sim_tlb_handle.hpp"
#include "register_access.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/simulation/tt_sim_communicator.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/tlb.hpp"

namespace tt::umd {

TTSimTlbWindow::TTSimTlbWindow(std::unique_ptr<TlbHandle> handle, TTSimCommunicator* communicator) :
    tlb_handle_(std::move(handle)), sim_communicator_(communicator) {
    offset_from_aligned_addr_ = configure_tlb_aligned(*tlb_handle_, tlb_data{});
}

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

void TTSimTlbWindow::write64(uint64_t offset, uint64_t value) { write_block(offset, &value, sizeof(value)); }

uint64_t TTSimTlbWindow::read64(uint64_t offset) {
    uint64_t value = 0;
    read_block(offset, &value, sizeof(value));
    return value;
}

void TTSimTlbWindow::write_aligned(uint64_t offset, const void* data, size_t size) {
    validate_tlb_window_aligned(offset, size, "write_aligned");
    write_registers_individually(*this, offset, data, size);
}

void TTSimTlbWindow::read_aligned(uint64_t offset, void* data, size_t size) {
    validate_tlb_window_aligned(offset, size, "read_aligned");
    read_registers_individually(*this, offset, data, size);
}

void TTSimTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    validate_tlb_window_range(offset, size, get_size());
    // Quasar's TLB handle programs no TLB registers and is left without a base address, so a
    // physical address derived from it carries no identity for the core this window was created
    // for. Name the core explicitly. Wormhole and Blackhole keep the physical-address path.
    //
    // This is an arch check rather than a capability check because the simulator does not model
    // Quasar TLBs at all. If it ever does, this needs to become a capability check.
    if (tlb_handle_->get_arch() != tt::ARCH::QUASAR) {
        sim_communicator_->pci_mem_write_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
        return;
    }
    // configure() stored local_offset in units of the TLB size, so scale it back to a device address.
    const tlb_data& config = tlb_handle_->get_config();
    const uint64_t device_addr = config.local_offset * tlb_handle_->get_size() + offset_from_aligned_addr_ + offset;
    sim_communicator_->tile_write_bytes(
        static_cast<uint32_t>(config.x_end),
        static_cast<uint32_t>(config.y_end),
        device_addr,
        data,
        static_cast<uint32_t>(size));
}

void TTSimTlbWindow::read_block(uint64_t offset, void* data, size_t size) {
    validate_tlb_window_range(offset, size, get_size());
    if (tlb_handle_->get_arch() != tt::ARCH::QUASAR) {
        sim_communicator_->pci_mem_read_bytes(get_physical_address(offset), data, static_cast<uint32_t>(size));
        return;
    }
    const tlb_data& config = tlb_handle_->get_config();
    const uint64_t device_addr = config.local_offset * tlb_handle_->get_size() + offset_from_aligned_addr_ + offset;
    sim_communicator_->tile_read_bytes(
        static_cast<uint32_t>(config.x_end),
        static_cast<uint32_t>(config.y_end),
        device_addr,
        data,
        static_cast<uint32_t>(size));
}

void TTSimTlbWindow::configure(const TargetIoWindowConfig& config) {
    configure(config, default_tlb_ordering(/*carries_ordering=*/true));
}

void TTSimTlbWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) {
    offset_from_aligned_addr_ = configure_tlb_aligned(
        *tlb_handle_,
        make_tlb_config(tlb_handle_->get_arch(), config, ordering, WindowFlags::None, /*carries_ordering=*/true));
}

// configure() accepts no non-direction flag, so there are none to report. The handle stores local_offset in
// units of the TLB size, so the reported addr is not the device address (#4126).
TargetIoWindowConfig TTSimTlbWindow::get_target_config() const {
    const tlb_data& config = tlb_handle_->get_config();
    return make_target_config(config, config.local_offset + offset_from_aligned_addr_, WindowFlags::None);
}

IoOrdering TTSimTlbWindow::get_io_ordering() const { return get_tlb_ordering(tlb_handle_->get_config()); }

size_t TTSimTlbWindow::get_size() const { return tlb_handle_->get_size() - offset_from_aligned_addr_; }

HostMemoryCaching TTSimTlbWindow::get_memory_caching_type() const {
    return get_tlb_memory_caching(tlb_handle_->get_tlb_mapping());
}

uint64_t TTSimTlbWindow::get_physical_address(uint64_t offset) const {
    // Get the base address from the TLB handle and add the offset
    // This should give us the physical address in the simulated device memory space.
    return reinterpret_cast<uint64_t>(tlb_handle_->get_base()) + offset_from_aligned_addr_ + offset;
}

}  // namespace tt::umd
