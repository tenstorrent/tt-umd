// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/rtl_sim_tlb_window.hpp"

#include <utility>

#include "umd/device/pcie/rtl_sim_tlb_handle.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/simulation/rtl_sim_communicator.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/io_options.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

namespace {

// Bit positions in the AXI awuser/aruser word, per the NPU-in AXI master layout.
constexpr int kAxiUserBitSnoop = 8;  // cce_cmd_snoop

uint64_t user_bits_from_io_options(const IoOptions& opts) {
    uint64_t bits = 0;
    if (opts.snoop) {
        bits |= (1ULL << kAxiUserBitSnoop);
    }
    return bits;
}

}  // namespace

RtlSimTlbWindow::RtlSimTlbWindow(
    std::unique_ptr<TlbHandle> handle, RtlSimCommunicator* communicator, const tlb_data config) :
    TlbWindow(std::move(handle), config), communicator_(communicator) {}

void RtlSimTlbWindow::translate_and_write(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    communicator_->tile_write_bytes(
        config.x_end,
        config.y_end,
        device_addr,
        data,
        static_cast<uint32_t>(size),
        user_bits_from_io_options(io_options_));
}

void RtlSimTlbWindow::translate_and_read(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    communicator_->tile_read_bytes(
        config.x_end,
        config.y_end,
        device_addr,
        data,
        static_cast<uint32_t>(size),
        user_bits_from_io_options(io_options_));
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

void RtlSimTlbWindow::safe_write16(uint64_t offset, uint16_t value) { write16(offset, value); }

uint16_t RtlSimTlbWindow::safe_read16(uint64_t offset) { return read16(offset); }

}  // namespace tt::umd
