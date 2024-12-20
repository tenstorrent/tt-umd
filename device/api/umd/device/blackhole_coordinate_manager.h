/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/blackhole_implementation.h"
#include "umd/device/coordinate_manager.h"

class BlackholeCoordinateManager : public CoordinateManager {
public:
    BlackholeCoordinateManager(
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
        const std::vector<tt_xy_pair>& pcie_cores);

protected:
    void translate_dram_coords() override;
    void translate_tensix_coords() override;

    void fill_tensix_physical_translated_mapping() override;
    void fill_eth_physical_translated_mapping() override;
    void fill_pcie_physical_translated_mapping() override;
    void fill_dram_physical_translated_mapping() override;

    std::vector<tt::umd::CoreCoord> get_tensix_cores() const override;
    std::vector<tt::umd::CoreCoord> get_harvested_tensix_cores() const override;
    std::vector<tt::umd::CoreCoord> get_dram_cores() const override;
    std::vector<tt::umd::CoreCoord> get_harvested_dram_cores() const override;
    tt_xy_pair get_tensix_grid_size() const override;
    tt_xy_pair get_dram_grid_size() const override;
    tt_xy_pair get_harvested_tensix_grid_size() const override;
    tt_xy_pair get_harvested_dram_grid_size() const override;

private:
    void map_column_of_dram_banks(const size_t start_bank, const size_t end_bank, const size_t x_coord);
};
