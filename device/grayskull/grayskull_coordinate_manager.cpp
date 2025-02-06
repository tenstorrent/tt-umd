/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/grayskull_coordinate_manager.h"

#include "logger.hpp"

using namespace tt::umd;

GrayskullCoordinateManager::GrayskullCoordinateManager(
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
    const std::vector<tt_xy_pair>& router_cores) :
    CoordinateManager(
        false,
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
        router_cores) {
    initialize();
}

void GrayskullCoordinateManager::fill_tensix_physical_translated_mapping() {
    throw std::runtime_error("NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_eth_physical_translated_mapping() {
    throw std::runtime_error("NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_dram_physical_translated_mapping() {
    throw std::runtime_error("NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_pcie_physical_translated_mapping() {
    throw std::runtime_error("NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_arc_physical_translated_mapping() {
    throw std::runtime_error("NOC translation is not supported for Grayskull.");
}
