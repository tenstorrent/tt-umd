/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/pcie/tlb_handle.hpp"

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <stdexcept>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "ioctl.h"

namespace tt::umd {

TlbHandle::TlbHandle(tt_device_t* tt_device, size_t size, const TlbMapping tlb_mapping) :
    tlb_size(size), tt_device_(tt_device), tlb_mapping(tlb_mapping) {
    int ret_code = tt_tlb_alloc(
        tt_device_, size, tlb_mapping == TlbMapping::UC ? TT_MMIO_CACHE_MODE_UC : TT_MMIO_CACHE_MODE_WC, &tlb_handle_);

    if (ret_code != 0) {
        TT_THROW("tt_tlb_alloc failed with error code {} for TLB size {}.", ret_code, size);
    }

    tt_tlb_get_id(tlb_handle_, reinterpret_cast<uint32_t*>(&tlb_id));

    tt_tlb_get_mmio(tlb_handle_, reinterpret_cast<void**>(&tlb_base));
}

TlbHandle::~TlbHandle() noexcept { free_tlb(); }

void TlbHandle::configure(const tlb_data& new_config) {
    tt_noc_addr_config_t config{};
    config.addr = new_config.local_offset;
    config.x_end = new_config.x_end;
    config.y_end = new_config.y_end;
    config.x_start = new_config.x_start;
    config.y_start = new_config.y_start;
    config.noc = new_config.noc_sel;
    config.mcast = new_config.mcast;
    config.ordering = new_config.ordering;
    config.static_vc = new_config.static_vc;

    int ret_code = tt_tlb_map(tt_device_, tlb_handle_, &config);

    if (ret_code != 0) {
        TT_THROW("tt_tlb_map failed with error code {} for TLB size {}.", ret_code, tlb_size);
    }

    tlb_config = new_config;
}

uint8_t* TlbHandle::get_base() { return tlb_base; }

size_t TlbHandle::get_size() const { return tlb_size; }

const tlb_data& TlbHandle::get_config() const { return tlb_config; }

const TlbMapping TlbHandle::get_tlb_mapping() const { return tlb_mapping; }

void TlbHandle::free_tlb() noexcept { tt_tlb_free(tt_device_, tlb_handle_); }

int TlbHandle::get_tlb_id() const { return tlb_id; }

}  // namespace tt::umd
