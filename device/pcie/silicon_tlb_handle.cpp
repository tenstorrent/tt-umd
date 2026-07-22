// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/silicon_tlb_handle.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "tracy.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/kmd_versions.hpp"

namespace tt::umd {

SiliconTlbHandle::SiliconTlbHandle(PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping) :
    pci_device_(pci_device) {
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
    // Use PCIDevice's configure_tlb method instead of KMD ioctl calls
    // This configures TLB registers directly in user space via BAR0.
    // PCIDevice' configure methods expects exact bits going into TLB configuration registers.
    // Since these bits are highest bits of the address based on TLB size, we need to shift the local_offset accordingly
    // before passing to configure_tlb.
    tlb_data cfg_data = new_config;
    cfg_data.local_offset = cfg_data.local_offset / get_size();
    pci_device_.configure_tlb(tlb_id_, cfg_data);

    tlb_config_ = new_config;
}

void SiliconTlbHandle::free_tlb() noexcept {
    TracyFreeN(tlb_base_, "TLB");
    tt_tlb_free(pci_device_.get_tt_device_handle(), tlb_handle_);
}

tt::ARCH SiliconTlbHandle::get_arch() const { return pci_device_.get_arch(); }

int SiliconTlbHandle::export_dmabuf(uint64_t offset, uint64_t size) const {
    if (!PCIDevice::is_tlb_dmabuf_export_supported()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Exporting a TLB as a dma-buf requires KMD >= {}.", KMD_TLB_DMABUF_EXPORT.str()));
    }

    int fd;
    int ret_code = tt_tlb_export_dmabuf(pci_device_.get_tt_device_handle(), tlb_handle_, offset, size, &fd);

    if (ret_code != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("tt_tlb_export_dmabuf failed with error code {}.", ret_code));
    }

    return fd;
}

}  // namespace tt::umd
