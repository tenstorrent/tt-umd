// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/rtl_sim_tlb_window.hpp"

#include <cstddef>
#include <cstdint>

#include "umd/device/chip_helpers/simulation_tlb_manager.hpp"
#include "umd/device/pcie/rtl_sim_tlb_handle.hpp"
#include "umd/device/simulation/rtl_sim_communicator.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

RtlSimTlbWindow::RtlSimTlbWindow(
    std::unique_ptr<TlbHandle> handle, RtlSimCommunicator* communicator, const tlb_data config) :
    TlbWindow(std::move(handle), config), communicator_(communicator) {}

void RtlSimTlbWindow::translate_and_write(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    communicator_->tile_write_bytes(config.x_end, config.y_end, device_addr, data, static_cast<uint32_t>(size));
}

void RtlSimTlbWindow::translate_and_read(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    communicator_->tile_read_bytes(config.x_end, config.y_end, device_addr, data, static_cast<uint32_t>(size));
}

void RtlSimTlbWindow::write32(uint64_t offset, uint32_t value) { translate_and_write(offset, &value, sizeof(value)); }

uint32_t RtlSimTlbWindow::read32(uint64_t offset) {
    uint32_t value = 0;
    translate_and_read(offset, &value, sizeof(value));
    return value;
}

void RtlSimTlbWindow::write_register(uint64_t offset, const void* data, size_t size) {
    translate_and_write(offset, data, size);
}

void RtlSimTlbWindow::read_register(uint64_t offset, void* data, size_t size) {
    translate_and_read(offset, data, size);
}

void RtlSimTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    translate_and_write(offset, data, size);
}

void RtlSimTlbWindow::read_block(uint64_t offset, void* data, size_t size) { translate_and_read(offset, data, size); }

void RtlSimTlbWindow::safe_write32(uint64_t offset, uint32_t value) { write32(offset, value); }

uint32_t RtlSimTlbWindow::safe_read32(uint64_t offset) { return read32(offset); }

void RtlSimTlbWindow::safe_write_register(uint64_t offset, const void* data, size_t size) {
    write_register(offset, data, size);
}

void RtlSimTlbWindow::safe_read_register(uint64_t offset, void* data, size_t size) {
    read_register(offset, data, size);
}

void RtlSimTlbWindow::safe_write_block(uint64_t offset, const void* data, size_t size) {
    write_block(offset, data, size);
}

void RtlSimTlbWindow::safe_read_block(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void RtlSimTlbWindow::safe_write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    write_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void RtlSimTlbWindow::safe_read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    read_block_reconfigure(mem_ptr, core, addr, size, ordering);
}

void RtlSimTlbWindow::safe_noc_multicast_write_reconfigure(
    void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    noc_multicast_write_reconfigure(dst, size, core_start, core_end, addr, ordering);
}

tt::ARCH RtlSimTlbWindow::get_arch() const {
    RtlSimTlbHandle* handle = dynamic_cast<RtlSimTlbHandle*>(tlb_handle.get());
    return handle->get_tlb_manager()->get_tt_device()->get_arch();
}

}  // namespace tt::umd
