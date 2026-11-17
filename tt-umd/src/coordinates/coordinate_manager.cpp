// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/coordinates/coordinate_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch,
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
    const std::vector<uint32_t>& noc0_y_to_noc1_y) {
    UMD_NOT_IMPLEMENTED();
}

std::shared_ptr<CoordinateManager> CoordinateManager::create_coordinate_manager(
    tt::ARCH arch,
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
    const BoardType board_type,
    const uint8_t asic_location) {
    UMD_NOT_IMPLEMENTED();
}

size_t CoordinateManager::get_num_harvested(const size_t harvesting_mask) { UMD_NOT_IMPLEMENTED(); }

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask(tt::ARCH arch, uint32_t tensix_harvesting_physical_layout) {
    UMD_NOT_IMPLEMENTED();
}

uint32_t CoordinateManager::shuffle_tensix_harvesting_mask_to_noc0_coords(
    tt::ARCH arch, uint32_t tensix_harvesting_logical_layout) {
    UMD_NOT_IMPLEMENTED();
}

uint32_t CoordinateManager::shuffle_l2cpu_harvesting_mask(tt::ARCH arch, uint32_t l2cpu_enabled_physical_layout) {
    UMD_NOT_IMPLEMENTED();
}

CoreCoord CoordinateManager::translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

CoreCoord CoordinateManager::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

CoreCoord CoordinateManager::translate_coord_to(
    const tt_xy_pair core, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<CoreCoord> CoordinateManager::get_cores(const CoreType core_type) const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair CoordinateManager::get_grid_size(const CoreType core_type) const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_harvested_cores(const CoreType core_type) const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair CoordinateManager::get_harvested_grid_size(const CoreType core_type) const { UMD_NOT_IMPLEMENTED(); }

HarvestingMasks CoordinateManager::get_harvesting_masks() const { UMD_NOT_IMPLEMENTED(); }

uint32_t CoordinateManager::get_num_eth_channels() const { UMD_NOT_IMPLEMENTED(); }

uint32_t CoordinateManager::get_num_harvested_eth_channels() const { UMD_NOT_IMPLEMENTED(); }

const std::vector<tt_xy_pair>& CoordinateManager::get_noc0_pairs(const CoreType core_type) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<CoreCoord> CoordinateManager::get_all_noc0_cores(const CoreType core_type) const { UMD_NOT_IMPLEMENTED(); }

CoordinateManager::CoordinateManager(
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
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
    num_eth_channels(eth_cores.size()) {
    UMD_NOT_IMPLEMENTED();
}

void CoordinateManager::initialize() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::assert_coordinate_manager_constructor() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_tensix_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_dram_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_eth_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_arc_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_pcie_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_router_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_security_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_l2cpu_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::translate_dispatch_coords() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::identity_map_noc0_cores() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::add_core_translation(const CoreCoord& core_coord, const tt_xy_pair& noc0_pair) {
    UMD_NOT_IMPLEMENTED();
}

void CoordinateManager::add_noc1_to_noc0_mapping() { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_tensix_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_harvested_tensix_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_dram_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_harvested_dram_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_eth_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_harvested_eth_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_pcie_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_harvested_pcie_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_l2cpu_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_harvested_l2cpu_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_dispatch_cores() const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> CoordinateManager::get_harvested_dispatch_cores() const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair CoordinateManager::get_dram_grid_size() const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair CoordinateManager::get_harvested_tensix_grid_size() const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair CoordinateManager::get_harvested_dram_grid_size() const { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::fill_tensix_default_noc0_translated_mapping() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::fill_eth_default_noc0_translated_mapping() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::fill_dram_default_noc0_translated_mapping() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::fill_pcie_default_noc0_translated_mapping() { UMD_NOT_IMPLEMENTED(); }

void CoordinateManager::fill_arc_default_noc0_translated_mapping() { UMD_NOT_IMPLEMENTED(); }

std::vector<size_t> CoordinateManager::get_harvested_indices(const size_t harvesting_mask) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
