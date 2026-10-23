// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_handle.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "tracy.hpp"
#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

SiliconTlbHandle::SiliconTlbHandle(PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping) :
    SiliconTlbHandle(pci_device, size, tlb_mapping, false) {}

SiliconTlbHandle::SiliconTlbHandle(
    PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping, const bool verify_config) :
    TlbHandle(verify_config), pci_device_(pci_device) {
    tlb_size_ = size;
    tlb_mapping_ = tlb_mapping;

    tt_device_t* tt_device = pci_device_.get_tt_device_handle();

    int ret_code = tt_tlb_alloc(
        tt_device, size, tlb_mapping_ == TlbMapping::UC ? TT_MMIO_CACHE_MODE_UC : TT_MMIO_CACHE_MODE_WC, &tlb_handle_);

    if (ret_code != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("tt_tlb_alloc failed with error code {} for TLB size {}.", ret_code, size));
    }

    tt_tlb_get_id(tlb_handle_, reinterpret_cast<uint32_t*>(&tlb_id_));

    tt_tlb_get_mmio(tlb_handle_, reinterpret_cast<void**>(&tlb_base_));
    TracyAllocN(tlb_base_, tlb_size_, "TLB");
}

SiliconTlbHandle::~SiliconTlbHandle() noexcept { SiliconTlbHandle::free_tlb(); }

void SiliconTlbHandle::configure(const tlb_data& new_config) {
    // How the window is actually programmed is PCIDevice's to decide: by storing to the
    // configuration registers in BAR0, or, where the host cannot reach them, by asking the driver.
    // Either way it takes a byte address, so nothing is shifted here.
    pci_device_.configure_tlb(tlb_id_, tlb_handle_, new_config, get_verify_config());

    tlb_config_ = new_config;
}

void SiliconTlbHandle::free_tlb() noexcept {
    TracyFreeN(tlb_base_, "TLB");
    tt_tlb_free(pci_device_.get_tt_device_handle(), tlb_handle_);
}

tt::ARCH SiliconTlbHandle::get_arch() const { return pci_device_.get_arch(); }

}  // namespace tt::umd
