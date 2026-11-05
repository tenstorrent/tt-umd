// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/rtl_sim_tlb_window.hpp"

#include <functional>
#include <utility>

#include "pcie/rtl_sim_tlb_handle.hpp"
#include "register_access.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/tlb.hpp"
#include "tt_device/protocol/rtl_sim_protocol.hpp"

namespace tt::umd {

namespace {

// Bit positions in the AXI awuser/aruser word, per the NPU-in AXI master layout.
constexpr int kAxiUserBitSnoop = 8;  // cce_cmd_snoop

uint64_t user_bits_from_window_flags(WindowFlags flags) {
    uint64_t bits = 0;
    if ((flags & WindowFlags::Snoop) != WindowFlags::None) {
        bits |= (1ULL << kAxiUserBitSnoop);
    }
    return bits;
}

}  // namespace

RtlSimTlbWindow::RtlSimTlbWindow(
    std::unique_ptr<TlbHandle> handle,
    RtlSimProtocol* protocol,
    const att::EndpointResolver* resolver,
    const SocDescriptor* soc_descriptor,
    const tlb_data config) :
    TlbWindow(std::move(handle), config), protocol_(protocol), resolver_(resolver), soc_descriptor_(soc_descriptor) {}

void RtlSimTlbWindow::translate_and_write(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    const tt_xy_pair core(config.x_end, config.y_end);
    const uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    const uint64_t user_bits = user_bits_from_window_flags(window_flags_);
    if (resolver_ == nullptr) {
        protocol_->tile_write_bytes(core, device_addr, data, size, user_bits);
        return;
    }
    // In flat-address mode the address alone names the destination, so no coordinate travels with it.
    const uint64_t flat_addr = att::resolve_translated(*resolver_, *soc_descriptor_, core, device_addr, size);
    protocol_->write_data(data, tt_xy_pair(0, 0), flat_addr, size, NocId::NOC0);
}

void RtlSimTlbWindow::translate_and_read(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    const tt_xy_pair core(config.x_end, config.y_end);
    const uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    const uint64_t user_bits = user_bits_from_window_flags(window_flags_);
    if (resolver_ == nullptr) {
        protocol_->tile_read_bytes(core, device_addr, data, size, user_bits);
        return;
    }
    const uint64_t flat_addr = att::resolve_translated(*resolver_, *soc_descriptor_, core, device_addr, size);
    protocol_->read_data(data, tt_xy_pair(0, 0), flat_addr, size, NocId::NOC0);
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

void RtlSimTlbWindow::write64(uint64_t offset, uint64_t value) { translate_and_write(offset, &value, sizeof(value)); }

uint64_t RtlSimTlbWindow::read64(uint64_t offset) {
    uint64_t value = 0;
    translate_and_read(offset, &value, sizeof(value));
    return value;
}

void RtlSimTlbWindow::write_register(uint64_t offset, const void* data, size_t size) {
    write_registers_individually(*this, offset, data, size);
}

void RtlSimTlbWindow::read_register(uint64_t offset, void* data, size_t size) {
    read_registers_individually(*this, offset, data, size);
}

void RtlSimTlbWindow::write_block(uint64_t offset, const void* data, size_t size) {
    translate_and_write(offset, data, size);
}

void RtlSimTlbWindow::read_block(uint64_t offset, void* data, size_t size) { translate_and_read(offset, data, size); }

}  // namespace tt::umd
