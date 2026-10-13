// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/rtl_sim_tlb_window.hpp"

#include <functional>
#include <utility>

#include "pcie/rtl_sim_tlb_handle.hpp"
#include "simulation/word_access.hpp"
#include "tt-umd/coordinates/att/att_resolver.hpp"
#include "tt-umd/pcie/tlb_handle.hpp"
#include "tt-umd/simulation/rtl_sim_communicator.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/tlb.hpp"

namespace tt::umd {

RtlSimTlbWindow::RtlSimTlbWindow(
    std::unique_ptr<TlbHandle> handle,
    RtlSimCommunicator* communicator,
    const att::EndpointResolver* resolver,
    const SocDescriptor* soc_descriptor,
    const tlb_data config) :
    TlbWindow(std::move(handle), config),
    communicator_(communicator),
    resolver_(resolver),
    soc_descriptor_(soc_descriptor) {}

void RtlSimTlbWindow::translate_and_write(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    const tt_xy_pair core(config.x_end, config.y_end);
    const uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    if (resolver_ == nullptr) {
        communicator_->tile_write_bytes(core.x, core.y, device_addr, data, static_cast<uint32_t>(size));
        return;
    }
    // In flat-address mode the address alone names the destination, and the simulator moves whole
    // words, so a partial first or last word is read, merged and written back (see word_access.hpp).
    const uint64_t flat_addr = att::resolve_translated(*resolver_, *soc_descriptor_, core, device_addr, size);
    write_bytes_as_words(
        flat_addr,
        data,
        static_cast<uint32_t>(size),
        [this](uint64_t a, void* d, uint32_t n) { communicator_->global_read_words(a, d, n); },
        [this](uint64_t a, const void* d, uint32_t n) { communicator_->global_write_words(a, d, n); });
}

void RtlSimTlbWindow::translate_and_read(uint64_t offset, void* data, size_t size) {
    validate(offset, size);
    const auto& config = tlb_handle->get_config();
    const tt_xy_pair core(config.x_end, config.y_end);
    const uint64_t device_addr = config.local_offset * tlb_handle->get_size() + get_total_offset(offset);
    if (resolver_ == nullptr) {
        communicator_->tile_read_bytes(core.x, core.y, device_addr, data, static_cast<uint32_t>(size));
        return;
    }
    const uint64_t flat_addr = att::resolve_translated(*resolver_, *soc_descriptor_, core, device_addr, size);
    read_bytes_as_words(flat_addr, data, static_cast<uint32_t>(size), [this](uint64_t a, void* d, uint32_t n) {
        communicator_->global_read_words(a, d, n);
    });
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
