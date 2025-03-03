/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_device/tlb_window.h"

namespace tt::umd {

class TlbAllocator {
public:
    TlbAllocator(PCIDevice* pci_device);

    ~TlbAllocator() = default;

    std::unique_ptr<TlbWindow> get_tlb(const uint64_t size, const tenstorrent_noc_tlb_config config);

private:
    PCIDevice* pci_device;
};

}  // namespace tt::umd
