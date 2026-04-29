// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/kmd_tlb_allocator.hpp"

#include <exception>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

KmdTlbAllocator::KmdTlbAllocator(PCIDevice* pci_device, const architecture_implementation* arch_impl) :
    pci_device_(pci_device), arch_impl_(arch_impl) {}

std::unique_ptr<TlbHandle> KmdTlbAllocator::allocate(size_t size, TlbMapping mapping) {
    if (size != 0) {
        return pci_device_->allocate_tlb(size, mapping);
    }

    // Caller didn't specify a size — try arch-supported sizes in preference order.
    const std::vector<size_t>& possible_sizes = arch_impl_->get_tlb_sizes();
    for (const auto& s : possible_sizes) {
        try {
            return pci_device_->allocate_tlb(s, mapping);
        } catch (const std::exception& e) {
            log_error(LogUMD, "Failed to allocate TLB window of size {}: {}", s, e.what());
        }
    }

    UMD_THROW(error::RuntimeError, "Failed to allocate TLB window.");
}

}  // namespace tt::umd
