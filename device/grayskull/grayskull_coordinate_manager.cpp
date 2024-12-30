/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/grayskull_coordinate_manager.h"

#include "logger.hpp"

using namespace tt::umd;

GrayskullCoordinateManager::GrayskullCoordinateManager(
    const tt_xy_pair& tensix_grid_size,
    const std::vector<tt_xy_pair>& tensix_cores,
    const size_t tensix_harvesting_mask,
    const tt_xy_pair& dram_grid_size,
    const std::vector<tt_xy_pair>& dram_cores,
    const size_t dram_harvesting_mask,
    const tt_xy_pair& eth_grid_size,
    const std::vector<tt_xy_pair>& eth_cores,
    const size_t eth_harvesting_mask,
    const tt_xy_pair& arc_grid_size,
    const std::vector<tt_xy_pair>& arc_cores,
    const tt_xy_pair& pcie_grid_size,
    const std::vector<tt_xy_pair>& pcie_cores,
    const std::vector<tt_xy_pair>& router_cores) :
    CoordinateManager(
        false,
        tensix_grid_size,
        tensix_cores,
        tensix_harvesting_mask,
        dram_grid_size,
        dram_cores,
        dram_harvesting_mask,
        eth_grid_size,
        eth_cores,
        eth_harvesting_mask,
        arc_grid_size,
        arc_cores,
        pcie_grid_size,
        pcie_cores,
        router_cores) {
    initialize();
}

void GrayskullCoordinateManager::fill_tensix_physical_translated_mapping() {
    log_assert(false, "NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_eth_physical_translated_mapping() {
    log_assert(false, "NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_dram_physical_translated_mapping() {
    log_assert(false, "NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_pcie_physical_translated_mapping() {
    log_assert(false, "NOC translation is not supported for Grayskull.");
}

void GrayskullCoordinateManager::fill_arc_physical_translated_mapping() {
    log_assert(false, "NOC translation is not supported for Grayskull.");
}
