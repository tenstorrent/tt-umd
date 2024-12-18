/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "chip/tlb_manager.h"

#include "common/logger.hpp"
#include "device/types/tlb.h"
#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

TLBManager::TLBManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void TLBManager::configure_tlb(tt_xy_pair core, int32_t tlb_index, uint64_t address, uint64_t ordering) {
    log_assert(
        ordering == tlb_data::Strict || ordering == tlb_data::Posted || ordering == tlb_data::Relaxed,
        "Invalid ordering specified in Cluster::configure_tlb");
    log_debug(
        LogSiliconDriver,
        "Configuring TLB for chip: {} core: {} tlb_index: {} address: {} ordering: {}",
        logical_device_id,
        core.str(),
        tlb_index,
        address,
        ordering);
    log_assert(tlb_config_map_.find(tlb_index) == tlb_config_map_.end(), "TLB index already configured {}", tlb_index);

    tt_device_->set_dynamic_tlb(tlb_index, core, address, ordering);
    auto tlb_size = std::get<1>(tt_device_->get_architecture_implementation()->describe_tlb(tlb_index).value());
    tlb_config_map_.insert({tlb_index, (address / tlb_size) * tlb_size});
    map_core_to_tlb_.insert({core, tlb_index});
}

void TLBManager::set_dynamic_tlb(std::string fallback_tlb_name, int32_t tlb_index) {
    log_assert(
        dynamic_tlb_config_.find(fallback_tlb_name) == dynamic_tlb_config_.end(),
        "Dynamic TLB already configured for {}",
        fallback_tlb_name);
    dynamic_tlb_config_.insert({fallback_tlb_name, tlb_index});
}

void TLBManager::set_dynamic_tlb_ordering(std::string fallback_tlb_name, uint64_t ordering) {
    log_assert(
        dynamic_tlb_config_.find(fallback_tlb_name) != dynamic_tlb_config_.end(),
        "Dynamic TLB not configured {}",
        fallback_tlb_name);

    dynamic_tlb_ordering_modes_[fallback_tlb_name] = ordering;
}
};  // namespace tt::umd
