// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/rtl_sim_tlb_window.hpp"

#include <functional>
#include <utility>

#include "umd/device/pcie/rtl_sim_tlb_handle.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

RtlSimTlbWindow::RtlSimTlbWindow(
    std::unique_ptr<TlbHandle> handle, RtlSimulationTTDevice* device, const tlb_data config) :
    TlbWindow(std::move(handle), config), device_(device) {}

void RtlSimTlbWindow::translate_and_write(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    device_->resolved_tile_write(tt_xy_pair(config.x_end, config.y_end), device_addr, data, size);
}

void RtlSimTlbWindow::translate_and_read(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    device_->resolved_tile_read(tt_xy_pair(config.x_end, config.y_end), device_addr, data, size);
}

void RtlSimTlbWindow::write16(uint64_t offset, uint16_t value) { translate_and_write(offset, &value, sizeof(value)); }

uint16_t RtlSimTlbWindow::read16(uint64_t offset) {
    uint16_t value = 0;
    translate_and_read(offset, &value, sizeof(value));
    return value;
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

}  // namespace tt::umd
