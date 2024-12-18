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
    if (tlb_config_map.find(logical_device_id) == tlb_config_map.end()) {
        tlb_config_map.insert({logical_device_id, {}});
        map_core_to_tlb_per_chip.insert({logical_device_id, {}});
    }
    log_debug(
        LogSiliconDriver,
        "Configuring TLB for chip: {} core: {} tlb_index: {} address: {} ordering: {}",
        logical_device_id,
        core.str(),
        tlb_index,
        address,
        ordering);
    log_assert(
        tlb_config_map.at(logical_device_id).find(tlb_index) == tlb_config_map.at(logical_device_id).end(),
        "TLB index already configured {}",
        tlb_index);

    tt_device_->set_dynamic_tlb(tlb_index, core, address, harvested_coord_translation.at(logical_device_id), ordering);
    auto tlb_size = std::get<1>(tt_device_->get_architecture_implementation()->describe_tlb(tlb_index).value());
    tlb_config_map.at(logical_device_id).insert({tlb_index, (address / tlb_size) * tlb_size});
    map_core_to_tlb_per_chip.at(logical_device_id).insert({core, tlb_index});
}
};  // namespace tt::umd
