/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/tlb_manager.h"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_io.hpp"
#include "umd/device/types/tlb.h"

namespace tt::umd {

static constexpr uint64_t DEFAULT_ORDERING_MODE = tlb_data::Relaxed;

TLBManager::TLBManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void TLBManager::configure_tlb(
    tt_xy_pair core, tt_xy_pair translated_core, uint32_t tlb_size, uint64_t address, uint64_t ordering) {
    TT_ASSERT(
        ordering == tlb_data::Strict || ordering == tlb_data::Posted || ordering == tlb_data::Relaxed,
        "Invalid ordering specified in Cluster::configure_tlb");
    log_debug(
        LogSiliconDriver,
        "Configuring TLB for chip: {} core: {} size: {} address: {} ordering: {}",
        tt_device_->get_pci_device()->get_device_num(),
        core.str(),
        tlb_size,
        address,
        ordering);

    tlb_data config;
    config.local_offset = address;
    config.x_end = translated_core.x;
    config.y_end = translated_core.y;
    config.x_start = 0;
    config.y_start = 0;
    config.noc_sel = 0;
    config.mcast = 0;
    config.ordering = ordering;
    config.linked = 0;
    config.static_vc = 1;
    std::unique_ptr<TlbWindow> tlb_window =
        std::make_unique<TlbWindow>(tt_device_->get_pci_device()->allocate_tlb(tlb_size, TlbMapping::WC), config);

    tlb_config_map_.insert({tlb_window->handle_ref().get_tlb_id(), (address / tlb_size) * tlb_size});
    map_core_to_tlb_.insert({core, tlb_window->handle_ref().get_tlb_id()});
    tlb_windows_.insert({tlb_window->handle_ref().get_tlb_id(), std::move(tlb_window)});
}

TlbWindow* TLBManager::get_tlb_window(const tt_xy_pair core) {
    if (map_core_to_tlb_.find(core) != map_core_to_tlb_.end()) {
        return tlb_windows_.at(map_core_to_tlb_.at(core)).get();
    } else {
        throw std::runtime_error(fmt::format("TLB window fore core ({}, {}) not found.", core.x, core.y));
    }
}

bool TLBManager::is_tlb_mapped(tt_xy_pair core) { return map_core_to_tlb_.find(core) != map_core_to_tlb_.end(); }

bool TLBManager::is_tlb_mapped(tt_xy_pair core, uint64_t address, uint32_t size_in_bytes) {
    if (!is_tlb_mapped(core)) {
        return false;
    }

    TlbWindow* tlb_window = get_tlb_window(core);

    return tlb_window->get_base_address() <= address &&
           address + size_in_bytes <= tlb_window->get_base_address() + tlb_window->get_size();
}

// TODO(pjanevski): reimplement this function.
tt::Writer TLBManager::get_static_tlb_writer(tt_xy_pair core) {
    if (!is_tlb_mapped(core)) {
        throw std::runtime_error(fmt::format("TLBs not initialized for core: {}", core.str()));
    }

    if (!tt_device_->get_pci_device()->bar0) {
        throw std::runtime_error("No write-combined mapping for BAR0");
    }

    auto tlb_index = map_core_to_tlb_.at(core);
    auto tlb_data = tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index);

    auto* base = reinterpret_cast<uint8_t*>(tt_device_->get_pci_device()->bar0);

    return tt::Writer(base + tlb_data.tlb_offset, tlb_data.size);
}

tlb_configuration TLBManager::get_tlb_configuration(tt_xy_pair core) {
    TT_ASSERT(is_tlb_mapped(core), "TLB not mapped for core: {}", core.str());

    int tlb_index = map_core_to_tlb_.at(core);
    return tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index);
}

};  // namespace tt::umd
