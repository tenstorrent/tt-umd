/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <map>
#include <set>
#include <vector>

#include "umd/device/tt_arch_types.h"
#include "umd/device/tt_core_coordinates.h"
#include "umd/device/tt_xy_pair.h"

class CoordinateManager {
public:
    CoordinateManager(
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

    static std::shared_ptr<CoordinateManager> create_coordinate_manager(
        tt::ARCH arch,
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

    static std::shared_ptr<CoordinateManager> create_coordinate_manager(
        tt::ARCH arch, const size_t tensix_harvesting_mask = 0, const size_t dram_harvesting_mask = 0);

    CoordinateManager(CoordinateManager& other) = default;

    tt::umd::CoreCoord to(const tt::umd::CoreCoord core_coord, const CoordSystem coord_system);

    virtual ~CoordinateManager() = default;

private:
    tt::umd::CoreCoord to_physical(const tt::umd::CoreCoord core_coord);
    tt::umd::CoreCoord to_logical(const tt::umd::CoreCoord core_coord);
    tt::umd::CoreCoord to_virtual(const tt::umd::CoreCoord core_coord);
    tt::umd::CoreCoord to_translated(const tt::umd::CoreCoord core_coord);

protected:
    virtual void translate_tensix_coords();
    virtual void translate_dram_coords();
    virtual void translate_eth_coords();
    virtual void translate_arc_coords();
    virtual void translate_pcie_coords();

    /*
     * Fills the logical to translated mapping for the tensix cores.
     * By default, translated coordinates are the same as physical coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Wormhole and Blackhole coordinate managers
     * override this method to implement different mapping.
     */
    virtual void fill_tensix_logical_to_translated();

    /*
     * Fills the logical to translated mapping for the ethernet cores.
     * By default, translated coordinates are the same as physical coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Wormhole and Blackhole coordinate managers
     * override this method to implement different mapping.
     */
    virtual void fill_eth_logical_to_translated();

    /*
     * Fills the logical to translated mapping for the DRAM cores.
     * By default, translated coordinates are the same as physical coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Blackhole coordinate manager overrides
     * this method to implement different mapping.
     */
    virtual void fill_dram_logical_to_translated();

    /*
     * Fills the logical to translated mapping for the PCIE cores.
     * By default, translated coordinates are the same as physical coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Blackhole coordinate manager overrides
     * this method to implement different mapping.
     */
    virtual void fill_pcie_logical_to_translated();

    /*
     * Fills the logical to translated mapping for the ARC cores.
     * By default, translated coordinates are the same as physical coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method.
     */
    virtual void fill_arc_logical_to_translated();

    std::map<tt_xy_pair, tt::umd::CoreCoord> tensix_logical_to_translated;
    std::map<tt_xy_pair, tt::umd::CoreCoord> tensix_logical_to_virtual;
    std::map<tt_xy_pair, tt::umd::CoreCoord> tensix_logical_to_physical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> tensix_physical_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> tensix_virtual_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> tensix_translated_to_logical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> dram_logical_to_translated;
    std::map<tt_xy_pair, tt::umd::CoreCoord> dram_logical_to_virtual;
    std::map<tt_xy_pair, tt::umd::CoreCoord> dram_logical_to_physical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> dram_physical_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> dram_virtual_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> dram_translated_to_logical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> eth_logical_to_translated;
    std::map<tt_xy_pair, tt::umd::CoreCoord> eth_logical_to_virtual;
    std::map<tt_xy_pair, tt::umd::CoreCoord> eth_logical_to_physical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> eth_physical_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> eth_virtual_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> eth_translated_to_logical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> arc_logical_to_translated;
    std::map<tt_xy_pair, tt::umd::CoreCoord> arc_logical_to_virtual;
    std::map<tt_xy_pair, tt::umd::CoreCoord> arc_logical_to_physical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> arc_physical_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> arc_virtual_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> arc_translated_to_logical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> pcie_logical_to_translated;
    std::map<tt_xy_pair, tt::umd::CoreCoord> pcie_logical_to_virtual;
    std::map<tt_xy_pair, tt::umd::CoreCoord> pcie_logical_to_physical;

    std::map<tt_xy_pair, tt::umd::CoreCoord> pcie_physical_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> pcie_virtual_to_logical;
    std::map<tt_xy_pair, tt::umd::CoreCoord> pcie_translated_to_logical;

    std::map<tt_xy_pair, tt::umd::CoreCoord>& get_logical_to_translated(CoreType core_type);
    std::map<tt_xy_pair, tt::umd::CoreCoord>& get_logical_to_virtual(CoreType core_type);
    std::map<tt_xy_pair, tt::umd::CoreCoord>& get_logical_to_physical(CoreType core_type);

    std::map<tt_xy_pair, tt::umd::CoreCoord>& get_physical_to_logical(CoreType core_type);
    std::map<tt_xy_pair, tt::umd::CoreCoord>& get_virtual_to_logical(CoreType core_type);
    std::map<tt_xy_pair, tt::umd::CoreCoord>& get_translated_to_logical(CoreType core_type);

    const tt_xy_pair tensix_grid_size;
    const std::vector<tt_xy_pair>& tensix_cores;
    size_t tensix_harvesting_mask;

    const tt_xy_pair dram_grid_size;
    const std::vector<tt_xy_pair>& dram_cores;
    size_t dram_harvesting_mask;

    const tt_xy_pair eth_grid_size;
    const std::vector<tt_xy_pair>& eth_cores;

    const tt_xy_pair arc_grid_size;
    const std::vector<tt_xy_pair>& arc_cores;

    const tt_xy_pair pcie_grid_size;
    const std::vector<tt_xy_pair>& pcie_cores;
};
