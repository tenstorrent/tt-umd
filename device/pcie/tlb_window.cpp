// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/tlb_window.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

tlb_data TlbWindow::make_tlb_config(
    uint64_t addr, tt_xy_pair core_end, NocId noc_id, uint64_t ordering, bool mcast, tt_xy_pair core_start) const {
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.noc_sel = static_cast<uint64_t>(noc_id);
    config.ordering = ordering;
    config.static_vc = handle_ref().get_arch() != tt::ARCH::BLACKHOLE;
    if (mcast) {
        config.mcast = true;
        config.x_start = core_start.x;
        config.y_start = core_start.y;
    }
    return config;
}

template <typename buffer_pointer, typename io_operation>
void TlbWindow::transfer_and_reconfigure(tlb_data config, buffer_pointer buffer, size_t size, io_operation op) {
    while (size > 0) {
        configure(config);
        size_t transfer_size = std::min(size, get_size());
        op(buffer, transfer_size);
        size -= transfer_size;
        config.local_offset += transfer_size;
        buffer += transfer_size;
    }
}

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config) : tlb_handle(std::move(handle)) {
    tlb_data aligned_config = config;
    aligned_config.local_offset = config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = config.local_offset - (config.local_offset & ~(tlb_handle->get_size() - 1));
}

void TlbWindow::read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    transfer_and_reconfigure(
        make_tlb_config(addr, core, noc_id, ordering),
        static_cast<uint8_t*>(mem_ptr),
        size,
        [this](uint8_t* buf, size_t sz) { read_block(0, buf, sz); });
}

void TlbWindow::read_register_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    transfer_and_reconfigure(
        make_tlb_config(addr, core, noc_id, ordering),
        static_cast<uint8_t*>(mem_ptr),
        size,
        [this](uint8_t* buf, size_t sz) { read_register(0, buf, sz); });
}

void TlbWindow::write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    transfer_and_reconfigure(
        make_tlb_config(addr, core, noc_id, ordering),
        static_cast<const uint8_t*>(mem_ptr),
        size,
        [this](const uint8_t* buf, size_t sz) { write_block(0, buf, sz); });
}

void TlbWindow::write_register_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    transfer_and_reconfigure(
        make_tlb_config(addr, core, noc_id, ordering),
        static_cast<const uint8_t*>(mem_ptr),
        size,
        [this](const uint8_t* buf, size_t sz) { write_register(0, buf, sz); });
}

void TlbWindow::noc_multicast_write_reconfigure(
    const void* src,
    size_t size,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t addr,
    NocId noc_id,
    uint64_t ordering) {
    transfer_and_reconfigure(
        make_tlb_config(addr, core_end, noc_id, ordering, true, core_start),
        static_cast<const uint8_t*>(src),
        size,
        [this](const uint8_t* buf, size_t sz) { write_block(0, buf, sz); });
}

TlbHandle& TlbWindow::handle_ref() const { return *tlb_handle; }

size_t TlbWindow::get_size() const { return tlb_handle->get_size() - offset_from_aligned_addr; }

void TlbWindow::validate(uint64_t offset, size_t size) const {
    if ((offset + size) > get_size()) {
        throw std::out_of_range("Out of bounds access");
    }
}

void TlbWindow::configure(const tlb_data& new_config) {
    tlb_data aligned_config = new_config;
    aligned_config.local_offset = new_config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = new_config.local_offset - (new_config.local_offset & ~(tlb_handle->get_size() - 1));
}

uint64_t TlbWindow::get_total_offset(uint64_t offset) const { return offset + offset_from_aligned_addr; }

uint64_t TlbWindow::get_base_address() const {
    return handle_ref().get_config().local_offset + offset_from_aligned_addr;
}

void TlbWindow::safe_write32(uint64_t offset, uint32_t value) { write32(offset, value); }

uint32_t TlbWindow::safe_read32(uint64_t offset) { return read32(offset); }

void TlbWindow::safe_write_register(uint64_t offset, const void* data, size_t size) {
    write_register(offset, data, size);
}

void TlbWindow::safe_read_register(uint64_t offset, void* data, size_t size) { read_register(offset, data, size); }

void TlbWindow::safe_write_block(uint64_t offset, const void* data, size_t size) { write_block(offset, data, size); }

void TlbWindow::safe_read_block(uint64_t offset, void* data, size_t size) { read_block(offset, data, size); }

void TlbWindow::safe_write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    write_block_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    read_block_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_read_register_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    read_register_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_write_register_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering) {
    write_register_reconfigure(mem_ptr, core, addr, size, noc_id, ordering);
}

void TlbWindow::safe_noc_multicast_write_reconfigure(
    const void* src,
    size_t size,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t addr,
    NocId noc_id,
    uint64_t ordering) {
    noc_multicast_write_reconfigure(src, size, core_start, core_end, addr, noc_id, ordering);
}

}  // namespace tt::umd
