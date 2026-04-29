// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/chip_helpers/tlb_allocator.hpp"

namespace tt::umd {

class PCIDevice;
class architecture_implementation;

/**
 * Silicon TLB allocator. Delegates to KMD via PCIDevice::allocate_tlb.
 *
 * KMD owns the actual bookkeeping. This class only handles the size-fallback
 * loop when the caller passes size=0.
 */
class KmdTlbAllocator : public TlbAllocator {
public:
    KmdTlbAllocator(PCIDevice* pci_device, const architecture_implementation* arch_impl);

    std::unique_ptr<TlbHandle> allocate(size_t size, TlbMapping mapping) override;

private:
    PCIDevice* pci_device_ = nullptr;
    const architecture_implementation* arch_impl_ = nullptr;
};

}  // namespace tt::umd
