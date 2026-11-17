// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_handle.hpp"

#include <cstddef>

#include "common/unsupported.hpp"

namespace tt::umd {

SiliconTlbHandle::SiliconTlbHandle(PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping) :
    SiliconTlbHandle(pci_device, size, tlb_mapping, false) {}

SiliconTlbHandle::SiliconTlbHandle(
    PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping, const bool verify_config) :
    TlbHandle(verify_config), pci_device_(pci_device) {
    UMD_NOT_IMPLEMENTED();
}

SiliconTlbHandle::~SiliconTlbHandle() noexcept { free_tlb(); }

void SiliconTlbHandle::configure(const tlb_data& new_config) { UMD_NOT_IMPLEMENTED(); }

tt::ARCH SiliconTlbHandle::get_arch() const { UMD_NOT_IMPLEMENTED(); }

void SiliconTlbHandle::free_tlb() noexcept {}

}  // namespace tt::umd
