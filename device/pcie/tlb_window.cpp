/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/pcie/tlb_window.hpp"

#include <string.h>

#include <stdexcept>

#include "umd/device/pcie/pci_device.hpp"

namespace tt::umd {

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config) : tlb_handle(std::move(handle)) {
    tlb_data aligned_config = config;
    aligned_config.local_offset = config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = config.local_offset - (config.local_offset & ~(tlb_handle->get_size() - 1));
}

void TlbWindow::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));
    *reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset)) = value;
}

uint32_t TlbWindow::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));
    return *reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
}

void TlbWindow::write_register(uint64_t offset, uint32_t value) { write32(offset, value); }

uint32_t TlbWindow::read_register(uint64_t offset) { return read32(offset); }

void TlbWindow::write_block(uint64_t offset, const void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = static_cast<const uint32_t *>(data);
    auto *dst = reinterpret_cast<volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));

    validate(offset, size);

    if (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_to_device((void *)dst, src, size);
    } else {
        memcpy((void *)dst, (void *)src, size);
    }
}

void TlbWindow::read_block(uint64_t offset, void *data, size_t size) {
    size_t n = size / sizeof(uint32_t);
    auto *src = reinterpret_cast<const volatile uint32_t *>(tlb_handle->get_base() + get_total_offset(offset));
    auto *dst = static_cast<uint32_t *>(data);

    validate(offset, size);

    if (PCIDevice::get_pcie_arch() == tt::ARCH::WORMHOLE_B0) {
        memcpy_from_device(dst, (void *)src, size);
    } else {
        memcpy((void *)dst, (void *)src, size);
    }
}

TlbHandle &TlbWindow::handle_ref() const { return *tlb_handle; }

size_t TlbWindow::get_size() const { return tlb_handle->get_size() - offset_from_aligned_addr; }

void TlbWindow::validate(uint64_t offset, size_t size) const {
    if ((offset + size) > get_size()) {
        throw std::out_of_range("Out of bounds access");
    }
}

void TlbWindow::configure(const tlb_data &new_config) {
    tlb_data aligned_config = new_config;
    aligned_config.local_offset = new_config.local_offset & ~(tlb_handle->get_size() - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = new_config.local_offset - (new_config.local_offset & ~(tlb_handle->get_size() - 1));
}

uint64_t TlbWindow::get_total_offset(uint64_t offset) const { return offset + offset_from_aligned_addr; }

void TlbWindow::memcpy_from_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the source (device) pointer.
    const volatile copy_t *sp;

    std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
    unsigned int src_misalignment = src_addr % sizeof(copy_t);

    if (src_misalignment != 0) {
        sp = reinterpret_cast<copy_t *>(src_addr - src_misalignment);

        copy_t tmp = *sp++;

        auto leading_len = std::min(sizeof(tmp) - src_misalignment, num_bytes);
        memcpy(dest, reinterpret_cast<char *>(&tmp) + src_misalignment, leading_len);
        num_bytes -= leading_len;
        dest = static_cast<char *>(dest) + leading_len;

    } else {
        sp = static_cast<const volatile copy_t *>(src);
    }

    // Copy the source-aligned middle.
    copy_t *dp = static_cast<copy_t *>(dest);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *sp;
        memcpy(dp, &tmp, trailing_len);
    }
}

void TlbWindow::memcpy_to_device(void *dest, const void *src, std::size_t num_bytes) {
    typedef std::uint32_t copy_t;

    // Start by aligning the destination (device) pointer. If needed, do RMW to fix up the
    // first partial word.
    volatile copy_t *dp;

    std::uintptr_t dest_addr = reinterpret_cast<std::uintptr_t>(dest);
    unsigned int dest_misalignment = dest_addr % sizeof(copy_t);

    if (dest_misalignment != 0) {
        // Read-modify-write for the first dest element.
        dp = reinterpret_cast<copy_t *>(dest_addr - dest_misalignment);

        copy_t tmp = *dp;

        auto leading_len = std::min(sizeof(tmp) - dest_misalignment, num_bytes);

        memcpy(reinterpret_cast<char *>(&tmp) + dest_misalignment, src, leading_len);
        num_bytes -= leading_len;
        src = static_cast<const char *>(src) + leading_len;

        *dp++ = tmp;

    } else {
        dp = static_cast<copy_t *>(dest);
    }

    // Copy the destination-aligned middle.
    const copy_t *sp = static_cast<const copy_t *>(src);
    std::size_t num_words = num_bytes / sizeof(copy_t);

    for (std::size_t i = 0; i < num_words; i++) {
        *dp++ = *sp++;
    }

    // Finally copy any sub-word trailer, again RMW on the destination.
    auto trailing_len = num_bytes % sizeof(copy_t);
    if (trailing_len != 0) {
        copy_t tmp = *dp;

        memcpy(&tmp, sp, trailing_len);

        *dp++ = tmp;
    }
}

uint64_t TlbWindow::get_base_address() const {
    return handle_ref().get_config().local_offset + offset_from_aligned_addr;
}

}  // namespace tt::umd
