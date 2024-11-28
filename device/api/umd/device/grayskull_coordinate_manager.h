/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/coordinate_manager.h"

class GrayskullCoordinateManager : public CoordinateManager {
public:
    GrayskullCoordinateManager(
        const tt_xy_pair& tensix_grid_size,
        const std::vector<tt_xy_pair>& tensix_cores,
        const size_t tensix_harvesting_mask,
        const tt_xy_pair& dram_grid_size,
        const std::vector<tt_xy_pair>& dram_cores,
        const size_t dram_harvesting_mask,
        const tt_xy_pair& eth_grid_size,
        const std::vector<tt_xy_pair>& eth_cores,
        const tt_xy_pair& arc_grid_size,
        const std::vector<tt_xy_pair>& arc_cores,
        const tt_xy_pair& pcie_grid_size,
        const std::vector<tt_xy_pair>& pcie_cores) :
        CoordinateManager(
            tensix_grid_size,
            tensix_cores,
            tensix_harvesting_mask,
            dram_grid_size,
            dram_cores,
            dram_harvesting_mask,
            eth_grid_size,
            eth_cores,
            arc_grid_size,
            arc_cores,
            pcie_grid_size,
            pcie_cores) {
        this->translate_tensix_coords();
        this->translate_dram_coords();
        this->translate_eth_coords();
        this->translate_arc_coords();
        this->translate_pcie_coords();
    }

protected:
    void fill_eth_logical_to_translated() override;
};
