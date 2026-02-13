// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include "noc_access.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"

namespace tt::umd {

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config) : tlb_handle(std::move(handle)) {
    tlb_data aligned_config = config;
    aligned_config.local_offset = config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = config.local_offset - (config.local_offset & ~(tlb_handle->get_size() - 1));
}

void TlbWindow::read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    uint8_t* buffer_addr = static_cast<uint8_t*>(mem_ptr);
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = ordering;
    config.static_vc = PCIDevice::get_pcie_arch() != tt::ARCH::BLACKHOLE;

    while (size > 0) {
        configure(config);
        uint32_t tlb_size = get_size();
        uint32_t transfer_size = std::min(size, tlb_size);

        read_block(0, buffer_addr, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;

        config.local_offset = addr;
    }
}

void TlbWindow::write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    const uint8_t* buffer_addr = static_cast<const uint8_t*>(mem_ptr);
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = 0;
    config.ordering = 0;
    config.static_vc = 0;

    while (size > 0) {
        configure(config);
        uint32_t tlb_size = get_size();

        uint32_t transfer_size = std::min(size, tlb_size);

        write_block(0, buffer_addr, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;

        config.local_offset = addr;
    }
}

void TlbWindow::noc_multicast_write_reconfigure(
    void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    uint8_t* buffer_addr = static_cast<uint8_t*>(dst);
    tlb_data config{};
    config.local_offset = addr;
    config.x_start = core_start.x;
    config.y_start = core_start.y;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.mcast = true;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = ordering;
    config.static_vc = PCIDevice::get_pcie_arch() != tt::ARCH::BLACKHOLE;

    while (size > 0) {
        configure(config);
        size_t tlb_size = get_size();

        uint32_t transfer_size = std::min(size, tlb_size);

        write_block(0, buffer_addr, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;

        config.local_offset = addr;
    }
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

}  // namespace tt::umd
