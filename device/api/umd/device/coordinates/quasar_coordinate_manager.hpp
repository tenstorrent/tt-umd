// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt {
struct HarvestingMasks;
}  // namespace tt

namespace tt::umd {

// TODO (#2494): Quasar silicon NOC translation tables are not yet finalized.
// Until they are, this manager maps every core's NOC0 coordinate to itself in
// the TRANSLATED system. This is sufficient for simulation, where the SOC
// descriptor YAML supplies the actual NOC0 layout. Replace the fill_* overrides
// with real Quasar translated-coordinate logic once the hardware spec is known.
class QuasarCoordinateManager : public CoordinateManager {
public:
    QuasarCoordinateManager(
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
        const std::vector<uint32_t>& noc0_x_to_noc1_x = {},
        const std::vector<uint32_t>& noc0_y_to_noc1_y = {});

protected:
    void fill_tensix_noc0_translated_mapping() override;
    void fill_dram_noc0_translated_mapping() override;
    void fill_eth_noc0_translated_mapping() override;
    void fill_pcie_noc0_translated_mapping() override;
    void fill_arc_noc0_translated_mapping() override;

    tt_xy_pair get_tensix_grid_size() const override;
};

}  // namespace tt::umd
