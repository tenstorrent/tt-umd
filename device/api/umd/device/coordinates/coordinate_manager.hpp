// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class CoordinateManager {
public:
    CoordinateManager(CoordinateManager& other) = default;
    virtual ~CoordinateManager() = default;

    /*
     * Creates a Coordinate Manager object. Main function for creating CoordinateManager. Given the cores for all
     * core types, harvesting masks and NOC0 to NOC1 mapping, it creates a CoordinateManager object.
     */
    static std::shared_ptr<CoordinateManager> create_coordinate_manager(
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
        const std::vector<uint32_t>& noc0_x_to_noc1_x = {},
        const std::vector<uint32_t>& noc0_y_to_noc1_y = {});

    /**
     * Create CoordinateManager object. Generally, main function for creating CoordinateManager is the one above,
     * this is used as convenience for creating CoordinateManager for standard TT architectures/configurations.
     * Out of arch, board_type and asic_location we can determine all the cores needed to pass into creating
     * CoordinateManager. Board type and is_chip_remote are used only for Blackhole, since PCIe cores are different for
     * different boards and whether the chip is remote or not.
     *
     * @param arch Architecture of the device.
     * @param noc_translation_enabled Whether NOC translation is enabled.
     * @param harvesting_masks Harvesting masks to use.
     * @param board_type Board type of the device.
     * @param asic_location ASIC location of the device.
     * @return CoordinateManager object.
     */
    static std::shared_ptr<CoordinateManager> create_coordinate_manager(
        tt::ARCH arch,
        const bool noc_translation_enabled,
        const HarvestingMasks harvesting_masks = {0, 0, 0},
        const BoardType board_type = BoardType::UNKNOWN,
        const uint8_t asic_location = 0);

    /**
     * Get number of harvested rows/columns/channels from harvesting mask. It basically represents number of
     * bits set in the harvesting mask.
     *
     * @param harvesting_mask Harvesting mask to get number of harvested rows/columns/channels from.
     * @return Number of harvested rows/columns/channels.
     */
    static size_t get_num_harvested(const size_t harvesting_mask);

    /**
     * Harvesting mask is reported by hardware in the order of physical layout. This function returns a more suitable
     * representation in NOC0 layout: Bit 0 being set means the first row in NOC0 coords is harvested.
     *
     * @param arch Architecture of the device. Important because physical layouts differ between architectures.
     * @param tensix_harvesting_physical_layout Harvesting mask in physical layout.
     * @return Harvesting mask in NOC0 layout.
     */
    static uint32_t shuffle_tensix_harvesting_mask(tt::ARCH arch, uint32_t tensix_harvesting_physical_layout);

    // TODO: This function should be removed once the corresponding API is removed from Cluster.
    static uint32_t shuffle_tensix_harvesting_mask_to_noc0_coords(
        tt::ARCH arch, uint32_t tensix_harvesting_logical_layout);

    /**
     * Harvesting mask is reported by hardware in the order of physical layout. This function returns a more suitable
     * representation in NOC0 layout: Bit 0 being set means the first row in NOC0 coords is harvested.
     *
     * @param arch Architecture of the device. Important because physical layouts differ between architectures.
     * @param l2cpu_enabled_physical_layout Core enabled mask in physical layout.
     * @return Harvesting mask in NOC0 layout.
     */
    static uint32_t shuffle_l2cpu_harvesting_mask(tt::ARCH arch, uint32_t l2cpu_enabled_physical_layout);

    /**
     * Translate core coordinates to target coordinate system. Input coordinates will have both the core type
     * and coordinate system set. Translation has some usecases when the translation is not possible, for example
     * harvested cores don't have logical coordinate system.
     *
     * @param core_coord Core coordinates to translate.
     * @param coord_system Coordinate system to translate to.
     * @return Translated core coordinates with both core type and coordinate system set.
     */
    CoreCoord translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const;

    /**
     * Get core coordinates at given pair of coordinates in given coordinate system.
     *
     * @param core Pair of coordinates to get core for.
     * @param coord_system Coordinate system to get core in.
     * @return Core coordinates at given pair of coordinates in given coordinate system.
     */
    CoreCoord get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const;

    /**
     * Translate pair of coordinates from one CoordSystem to another. Returned coordinates will have both the core type
     * and coordinate system set. This function is useful if user doesn't care about core type of certain coordinates
     * wants just to translate it to some other coordinate system.
     *
     * @param core Pair of coordinates to translate.
     * @param input_coord_system Input coordinate system of the core.
     * @param target_coord_system Target coordinate system to translate
     * @return Translated coordinates with both core type and coordinate system set.
     */
    CoreCoord translate_coord_to(
        const tt_xy_pair core, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const;

    /**
     * Get all non harvested cores of given type. All cores are returned in NOC0 coordinates.
     *
     * @param core_type Core type to get harvested cores for.
     * @return Vector of cores in NOC0 coordinates.
     */
    std::vector<CoreCoord> get_cores(const CoreType core_type) const;

    /**
     * Get grid size of non harvested cores for a given core type. Usually we can represent cores in a grid, so this
     * represents the cores in a grid taking harvesting into consideration. This useful for checking whether we have
     * calculated the translation properly.
     *
     * @param core_type Core type to get grid size for.
     * @return Grid size of cores for a given core type.
     */
    tt_xy_pair get_grid_size(const CoreType core_type) const;

    /**
     * Get all harvested cores of given type. All cores are returned in NOC0 coordinates.
     *
     * @param core_type Core type to get harvested cores for.
     * @return Vector of harvested cores in NOC0 coordinates.
     */
    std::vector<CoreCoord> get_harvested_cores(const CoreType core_type) const;

    /**
     * Get grid size of harvested cores for a given core type. Usually we can represent cores in a grid, so this enabled
     * representing harvested cores in a grid as well. This useful for checking whether we have calculated the
     * translation properly.
     *
     * @param core_type Core type to get harvested grid size for.
     * @return Grid size of harvested cores for a given core type.
     */
    tt_xy_pair get_harvested_grid_size(const CoreType core_type) const;

    /**
     * Get harvesting masks CoordinateManager object was created with.
     * All harvesting masks are in NOC0 layout, meaning that bit 0 corresponds to the first row in NOC0 coordinates.
     *
     * @return Harvesting masks in NOC0 layout.
     */
    HarvestingMasks get_harvesting_masks() const;

    /**
     * Get number of Ethernet channels.
     *
     * @return Number of Ethernet channels.
     */
    uint32_t get_num_eth_channels() const;

    /**
     * Get number of harvested ETH channels.
     *
     * @return Number of harvested ETH channels.
     */
    uint32_t get_num_harvested_eth_channels() const;

private:
    const std::vector<tt_xy_pair>& get_noc0_pairs(const CoreType core_type) const;
    std::vector<CoreCoord> get_all_noc0_cores(const CoreType core_type) const;

    /**
     * Function that allows workarounds for the translated coordinate system to work for every core type.
     *
     * @return Right type for the right core.
     */
    virtual CoordSystem fix_translated_coord_system_hook(const CoordSystem target_coord_system) const {
        return target_coord_system;
    }

protected:
    /*
     * Constructor for Coordinate Manager.
     * Tensix harvesting mask is supposed to be passed as original harvesting mask that is
     * returned from create-ethernet-map, so each bit is responsible for one row of the actual noc0
     * row of the tensix cores on the chip. Harvesting mask is shuffled in constructor to match the NOC
     * layout of the tensix cores.
     * Router cores don't have a grid size, since they are not layed out in a regular fashion.
     */
    CoordinateManager(
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
        const std::vector<uint32_t>& noc0_x_to_noc1_x = {},
        const std::vector<uint32_t>& noc0_y_to_noc1_y = {});

    void initialize();

    virtual void assert_coordinate_manager_constructor();

    virtual void translate_tensix_coords();
    virtual void translate_dram_coords();
    virtual void translate_eth_coords();
    virtual void translate_arc_coords();
    virtual void translate_pcie_coords();
    virtual void translate_router_coords();
    virtual void translate_security_coords();
    virtual void translate_l2cpu_coords();

    void identity_map_noc0_cores();
    void add_core_translation(const CoreCoord& core_coord, const tt_xy_pair& noc0_pair);
    void add_noc1_to_noc0_mapping();

    virtual std::vector<CoreCoord> get_tensix_cores() const;
    virtual std::vector<CoreCoord> get_harvested_tensix_cores() const;
    virtual std::vector<CoreCoord> get_dram_cores() const;
    virtual std::vector<CoreCoord> get_harvested_dram_cores() const;
    virtual std::vector<CoreCoord> get_eth_cores() const;
    virtual std::vector<CoreCoord> get_harvested_eth_cores() const;
    virtual std::vector<CoreCoord> get_pcie_cores() const;
    virtual std::vector<CoreCoord> get_harvested_pcie_cores() const;
    virtual std::vector<CoreCoord> get_l2cpu_cores() const;
    virtual std::vector<CoreCoord> get_harvested_l2cpu_cores() const;
    virtual tt_xy_pair get_tensix_grid_size() const = 0;
    virtual tt_xy_pair get_dram_grid_size() const;
    virtual tt_xy_pair get_harvested_tensix_grid_size() const;
    virtual tt_xy_pair get_harvested_dram_grid_size() const;

    /*
     * By default, translated coordinates are the same as noc0 coordinates.
     * This will be true for all architectures if noc_translation_enabled is false.
     */
    void fill_tensix_default_noc0_translated_mapping();
    void fill_eth_default_noc0_translated_mapping();
    void fill_dram_default_noc0_translated_mapping();
    void fill_pcie_default_noc0_translated_mapping();
    void fill_arc_default_noc0_translated_mapping();

    /*
     * Fills the noc0 to translated mapping for the tensix cores.
     * By default, translated coordinates are the same as noc0 coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Wormhole and Blackhole coordinate managers
     * override this method to implement different mapping.
     */
    virtual void fill_tensix_noc0_translated_mapping() = 0;

    /*
     * Fills the noc0 to translated mapping for the ethernet cores.
     * By default, translated coordinates are the same as noc0 coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Wormhole and Blackhole coordinate managers
     * override this method to implement different mapping.
     */
    virtual void fill_eth_noc0_translated_mapping() = 0;

    /*
     * Fills the noc0 to translated mapping for the DRAM cores.
     * By default, translated coordinates are the same as noc0 coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Blackhole coordinate manager overrides
     * this method to implement different mapping.
     */
    virtual void fill_dram_noc0_translated_mapping() = 0;

    /*
     * Fills the noc0 to translated mapping for the PCIE cores.
     * By default, translated coordinates are the same as noc0 coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method. Blackhole coordinate manager overrides
     * this method to implement different mapping.
     */
    virtual void fill_pcie_noc0_translated_mapping() = 0;

    /*
     * Fills the noc0 to translated mapping for the ARC cores.
     * By default, translated coordinates are the same as noc0 coordinates.
     * Derived coordinate managers that need to implement different mapping
     * should override this method.
     */
    virtual void fill_arc_noc0_translated_mapping() = 0;

    static std::vector<size_t> get_harvested_indices(const size_t harvesting_mask);

    // Maps full CoreCoord from any CoordSystem to noc0 coordinates.
    std::map<CoreCoord, tt_xy_pair> to_noc0_map;
    // Maps noc0 coordinates given a target CoordSystem to full CoreCoord.
    std::map<std::pair<tt_xy_pair, CoordSystem>, CoreCoord> from_noc0_map;
    // Maps coordinates in the designated CoordSystem to a full CoreCoord at that location holding the right CoreType.
    // Doesn't include logical CoordSystem.
    std::map<std::pair<tt_xy_pair, CoordSystem>, CoreCoord> to_core_type_map;

    // Whether NOC translation is enabled on chip.
    // This flag affects how Translated coords are calculated. If translation is enabled on the chip, than we can
    // interface it with a coordinate system which abstracts away harvested cores. If it is not enabled, then we need to
    // interface it with noc0 coordinates.
    bool noc_translation_enabled;
    HarvestingMasks harvesting_masks;

    tt_xy_pair tensix_grid_size;
    const std::vector<tt_xy_pair> tensix_cores;

    tt_xy_pair dram_grid_size;
    const std::vector<tt_xy_pair> dram_cores;

    const size_t num_eth_channels;
    const std::vector<tt_xy_pair> eth_cores;

    tt_xy_pair arc_grid_size;
    const std::vector<tt_xy_pair> arc_cores;

    tt_xy_pair pcie_grid_size;
    const std::vector<tt_xy_pair> pcie_cores;

    // Router cores don't have a grid size, since they are not layed out in a regular fashion.
    const std::vector<tt_xy_pair> router_cores;

    const std::vector<tt_xy_pair> security_cores;

    const std::vector<tt_xy_pair> l2cpu_cores;

    const std::vector<uint32_t> noc0_x_to_noc1_x;
    const std::vector<uint32_t> noc0_y_to_noc1_y;
};

}  // namespace tt::umd
