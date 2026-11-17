// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_handle.hpp"

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

SiliconTlbHandle::SiliconTlbHandle(PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping) :
    SiliconTlbHandle(pci_device, size, tlb_mapping, false) {}

SiliconTlbHandle::SiliconTlbHandle(
    PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping, const bool verify_config) :
    TlbHandle(verify_config), pci_device_(pci_device) {
    UMD_THROW(error::RuntimeError, "SiliconTlbHandle::SiliconTlbHandle is not implemented yet");
}

SiliconTlbHandle::~SiliconTlbHandle() noexcept { free_tlb(); }

void SiliconTlbHandle::configure(const tlb_data& new_config) {
    UMD_THROW(error::RuntimeError, "SiliconTlbHandle::configure is not implemented yet");
}

tt::ARCH SiliconTlbHandle::get_arch() const { return pci_device_.get_arch(); }

void SiliconTlbHandle::free_tlb() noexcept {}

}  // namespace tt::umd
