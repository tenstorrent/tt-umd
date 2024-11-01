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
        const std::vector<tt_xy_pair>& pcie_cores) :
        tensix_grid_size(tensix_grid_size),
        tensix_cores(tensix_cores),
        tensix_harvesting_mask(tensix_harvesting_mask),
        dram_grid_size(dram_grid_size),
        dram_cores(dram_cores),
        dram_harvesting_mask(dram_harvesting_mask),
        eth_grid_size(eth_grid_size),
        eth_cores(eth_cores),
        arc_grid_size(arc_grid_size),
        arc_cores(arc_cores),
        pcie_grid_size(pcie_grid_size),
        pcie_cores(pcie_cores) {}

    static std::shared_ptr<CoordinateManager> get_coordinate_manager(
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

    static std::shared_ptr<CoordinateManager> get_coordinate_manager(
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
    virtual void tensix_harvesting(const size_t harvesting_mask);
    virtual void dram_harvesting(const size_t dram_harvesting_mask);
    virtual void translate_eth_coords();
    virtual void translate_arc_coords();
    virtual void translate_pcie_coords();

    void clear_tensix_harvesting_structures();
    void clear_dram_harvesting_structures();

    virtual void fill_tensix_logical_to_translated();
    virtual void fill_eth_logical_to_translated();
    virtual void fill_pcie_logical_to_translated();

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
