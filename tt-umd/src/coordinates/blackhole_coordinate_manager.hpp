// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tt-umd/coordinates/coordinate_manager.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

// Blackhole harvests Tensix columns, DRAM banks, ETH cores, one of the two PCIe cores and L2CPU
// clusters. With NOC translation on, translated coordinates follow what the firmware programs into
// the NIU translation tables.
class BlackholeCoordinateManager : public CoordinateManager {
public:
    BlackholeCoordinateManager(
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
        const std::vector<uint32_t>& noc0_x_to_noc1_x = {},
        const std::vector<uint32_t>& noc0_y_to_noc1_y = {});

    // Harvesting mask of the PCIe core the given board does not connect to the host, in the order of
    // blackhole::PCIE_CORES_NOC0. Zero when the board type does not determine it.
    static size_t get_pcie_harvesting_mask(BoardType board_type, uint8_t asic_location);

protected:
    void assert_coordinate_manager_constructor() override;

    void translate_tensix_coords() override;

    tt_xy_pair get_tensix_grid_size() const override;
    tt_xy_pair get_harvested_tensix_grid_size() const override;

    void fill_tensix_noc0_translated_mapping() override;
    void fill_eth_noc0_translated_mapping() override;
    void fill_dram_noc0_translated_mapping() override;
    void fill_pcie_noc0_translated_mapping() override;
    void fill_arc_noc0_translated_mapping() override;
};

}  // namespace tt::umd
