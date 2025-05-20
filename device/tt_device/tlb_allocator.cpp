/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/tlb_allocator.h"

namespace tt::umd {

TlbAllocator::TlbAllocator(PCIDevice* pci_device) : pci_device(pci_device) {}

std::unique_ptr<TlbWindow> TlbAllocator::get_tlb(const uint64_t size, const tenstorrent_noc_tlb_config config) {
    auto tlb_window = std::make_unique<TlbWindow>(std::make_unique<TlbHandle>(pci_device->get_fd(), size, config));
    tlb_window->configure(config);
    return tlb_window;
}

}  // namespace tt::umd
