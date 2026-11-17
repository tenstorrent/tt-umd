// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/silicon_tlb_handle.hpp"

#include <fmt/format.h>
#include <tt-kmd-lib/tt_kmd_lib.h>

#include <cstring>

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

SiliconTlbHandle::SiliconTlbHandle(PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping) :
    SiliconTlbHandle(pci_device, size, tlb_mapping, false) {}

SiliconTlbHandle::SiliconTlbHandle(
    PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping, const bool verify_config) :
    TlbHandle(verify_config), pci_device_(pci_device) {
    const tt_tlb_cache_mode cache_mode = tlb_mapping == TlbMapping::WC ? TT_MMIO_CACHE_MODE_WC : TT_MMIO_CACHE_MODE_UC;
    const int result = tt_tlb_alloc(pci_device_.get_tt_device_handle(), size, cache_mode, &tlb_handle_);
    if (result != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to allocate a {:#x}-byte TLB window on PCIe device {}: {}",
                size,
                pci_device_.get_device_num(),
                std::strerror(-result)));
    }

    void* mmio = nullptr;
    uint32_t id = 0;
    tt_tlb_get_mmio(tlb_handle_, &mmio);
    tt_tlb_get_id(tlb_handle_, &id);
    tlb_base_ = static_cast<uint8_t*>(mmio);
    tlb_id_ = static_cast<int>(id);
    tlb_size_ = size;
    tlb_mapping_ = tlb_mapping;
}

SiliconTlbHandle::~SiliconTlbHandle() noexcept { free_tlb(); }

void SiliconTlbHandle::configure(const tlb_data& new_config) {
    pci_device_.configure_tlb(tlb_id_, new_config, get_verify_config());
    tlb_config_ = new_config;
}

tt::ARCH SiliconTlbHandle::get_arch() const { return pci_device_.get_arch(); }

void SiliconTlbHandle::free_tlb() noexcept {
    if (tlb_handle_ != nullptr) {
        tt_tlb_free(pci_device_.get_tt_device_handle(), tlb_handle_);
        tlb_handle_ = nullptr;
    }
}

}  // namespace tt::umd
