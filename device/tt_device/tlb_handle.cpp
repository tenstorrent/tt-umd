/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/tlb_handle.h"

namespace tt::umd {

TlbHandle::TlbHandle(PCIDevice* pci_device, size_t size, const tenstorrent_noc_tlb_config& config) :
    tlb_size(size), pci_device(pci_device) {
    tenstorrent_allocate_tlb_in allocate_tlb_in;
    allocate_tlb_in.size = size;
    tenstorrent_allocate_tlb_out allocate_tlb_out = pci_device->allocate_tlb(allocate_tlb_in);
    tlb_id = allocate_tlb_out.id;

    try {
        configure(config);

        // mmap only UC offset for now.
        // TODO: add choice whether to map UC or WC mapping.
        void* uc = mmap(
            nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, pci_device->get_fd(), allocate_tlb_out.mmap_offset_uc);
        if (uc == MAP_FAILED) {
            munmap(uc, size);
            throw std::runtime_error("Failed to map TLB UC base  using mmap call.");
        }

        tlb_base = reinterpret_cast<uint8_t*>(uc);
    } catch (...) {
        free_tlb();
        throw;
    }
}

TlbHandle::~TlbHandle() noexcept {
    munmap(tlb_base, tlb_size);
    free_tlb();
}

void TlbHandle::configure(const tenstorrent_noc_tlb_config& new_config) {
    tenstorrent_configure_tlb_in configure_tlb_in;
    configure_tlb_in.id = tlb_id;
    configure_tlb_in.config = new_config;

    if (std::memcmp(&new_config, &tlb_config, sizeof(new_config)) == 0) {
        return;
    }

    pci_device->configure_tlb(configure_tlb_in);

    tlb_config = new_config;
}

uint8_t* TlbHandle::get_base() { return tlb_base; }

size_t TlbHandle::get_size() const { return tlb_size; }

const tenstorrent_noc_tlb_config& TlbHandle::get_config() const { return tlb_config; }

void TlbHandle::free_tlb() noexcept {
    tenstorrent_free_tlb_in free_tlb_in;
    free_tlb_in.id = tlb_id;
    pci_device->free_tlb(free_tlb_in);
}

}  // namespace tt::umd
