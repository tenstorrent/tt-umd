// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/tlb_handle.hpp"

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "ioctl.h"
#include "umd/device/pcie/pci_device.hpp"

namespace tt::umd {

TlbHandle::TlbHandle(PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping) :
    tlb_size(size), pci_device_(pci_device), tlb_mapping(tlb_mapping) {
    tt_device_t* tt_device = pci_device_.get_tt_device_handle();

    int ret_code = tt_tlb_alloc(
        tt_device, size, tlb_mapping == TlbMapping::UC ? TT_MMIO_CACHE_MODE_UC : TT_MMIO_CACHE_MODE_WC, &tlb_handle_);

    if (ret_code != 0) {
        TT_THROW("tt_tlb_alloc failed with error code {} for TLB size {}.", ret_code, size);
    }

    tt_tlb_get_id(tlb_handle_, reinterpret_cast<uint32_t*>(&tlb_id));

    tt_tlb_get_mmio(tlb_handle_, reinterpret_cast<void**>(&tlb_base));
}

TlbHandle::~TlbHandle() noexcept { free_tlb(); }

void TlbHandle::configure(const tlb_data& new_config) {
    // Use PCIDevice's configure_tlb method instead of KMD ioctl calls
    // This configures TLB registers directly in user space via BAR0.
    tlb_data cfg_data = new_config;
    cfg_data.local_offset = cfg_data.local_offset / get_size();
    pci_device_.configure_tlb(tlb_id, cfg_data);

    tlb_config = new_config;
}

uint8_t* TlbHandle::get_base() { return tlb_base; }

size_t TlbHandle::get_size() const { return tlb_size; }

const tlb_data& TlbHandle::get_config() const { return tlb_config; }

TlbMapping TlbHandle::get_tlb_mapping() const { return tlb_mapping; }

void TlbHandle::free_tlb() noexcept { tt_tlb_free(pci_device_.get_tt_device_handle(), tlb_handle_); }

int TlbHandle::get_tlb_id() const { return tlb_id; }

}  // namespace tt::umd
