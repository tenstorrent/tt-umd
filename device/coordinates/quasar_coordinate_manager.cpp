// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/coordinates/quasar_coordinate_manager.hpp"

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

QuasarCoordinateManager::QuasarCoordinateManager(
    const bool noc_translation_enabled,
    HarvestingMasks harvesting_masks,
    const tt_xy_pair& tensix_grid_size,
    const std::vector<tt_xy_pair>& tensix_cores,
    const tt_xy_pair& dram_grid_size,
    const std::vector<tt_xy_pair>& dram_cores,
    const std::vector<tt_xy_pair>& eth_cores,
    const tt_xy_pair& arc_grid_size,
    const std::vector<tt_xy_pair>& arc_cores,
    const tt_xy_pair& pcie_grid_size,
    const std::vector<tt_xy_pair>& pcie_cores,
    const std::vector<tt_xy_pair>& router_cores,
    const std::vector<tt_xy_pair>& security_cores,
    const std::vector<tt_xy_pair>& l2cpu_cores,
    const std::vector<tt_xy_pair>& dispatch_cores,
    const std::vector<uint32_t>& noc0_x_to_noc1_x,
    const std::vector<uint32_t>& noc0_y_to_noc1_y) :
    CoordinateManager(
        noc_translation_enabled,
        harvesting_masks,
        tensix_grid_size,
        tensix_cores,
        dram_grid_size,
        dram_cores,
        eth_cores,
        arc_grid_size,
        arc_cores,
        pcie_grid_size,
        pcie_cores,
        router_cores,
        security_cores,
        l2cpu_cores,
        dispatch_cores,
        noc0_x_to_noc1_x,
        noc0_y_to_noc1_y) {
    initialize();
}

// TODO (#2494): Replace identity translated mappings with real Quasar NOC
// translation once the hardware spec is finalized.
void QuasarCoordinateManager::fill_tensix_noc0_translated_mapping() { fill_tensix_default_noc0_translated_mapping(); }

void QuasarCoordinateManager::fill_dram_noc0_translated_mapping() { fill_dram_default_noc0_translated_mapping(); }

void QuasarCoordinateManager::fill_eth_noc0_translated_mapping() { fill_eth_default_noc0_translated_mapping(); }

void QuasarCoordinateManager::fill_pcie_noc0_translated_mapping() { fill_pcie_default_noc0_translated_mapping(); }

void QuasarCoordinateManager::fill_arc_noc0_translated_mapping() { fill_arc_default_noc0_translated_mapping(); }

tt_xy_pair QuasarCoordinateManager::get_tensix_grid_size() const {
    return {tensix_grid_size.x, tensix_grid_size.y - get_num_harvested(harvesting_masks.tensix_harvesting_mask)};
}

}  // namespace tt::umd
