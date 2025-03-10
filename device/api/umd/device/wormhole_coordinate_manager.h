/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/coordinate_manager.h"
#include "umd/device/wormhole_implementation.h"

class WormholeCoordinateManager : public CoordinateManager {
public:
    WormholeCoordinateManager(
        const bool noc_translation_enabled,
        tt::umd::HarvestingMasks harvesting_masks,
        const tt_xy_pair& tensix_grid_size,
        const std::vector<tt_xy_pair>& tensix_cores,
        const tt_xy_pair& dram_grid_size,
        const std::vector<tt_xy_pair>& dram_cores,
        const std::vector<tt_xy_pair>& eth_cores,
        const tt_xy_pair& arc_grid_size,
        const std::vector<tt_xy_pair>& arc_cores,
        const tt_xy_pair& pcie_grid_size,
        const std::vector<tt_xy_pair>& pcie_cores,
        const std::vector<tt_xy_pair>& router_cores);

protected:
    void fill_tensix_physical_translated_mapping() override;
    void fill_dram_physical_translated_mapping() override;
    void fill_eth_physical_translated_mapping() override;
    void fill_pcie_physical_translated_mapping() override;
    void fill_arc_physical_translated_mapping() override;
};
