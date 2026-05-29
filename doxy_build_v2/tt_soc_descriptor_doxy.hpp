// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tt_enums_structs_constants_doxy.hpp"

namespace tt::umd {
class CoordinateManager;
class SocArchDescriptor;

/**
 * @defgroup tt_soc_descriptor SocDescriptor
 * @{
 *
 * @brief Per-chip SoC descriptor combining static topology with runtime state.
 *
 * Wraps a @ref SocArchDescriptor (the architecture's full unharvested floorplan)
 * with per-chip runtime data: harvesting masks, NOC translation settings, and
 * a @ref CoordinateManager for coordinate system conversions.
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref SocArchDescriptor | Static architecture topology shared across chips |
 * | @ref CoordinateManager | Coordinate translation accounting for harvesting |
 * | @ref ChipInfo | Per-chip identity: harvesting masks, board type, NOC translation |
 * | @ref CoreCoord | Typed coordinate with coordinate system tag |
 * | @ref CoordSystem | Coordinate system selector (PHYSICAL, LOGICAL, TRANSLATED, etc.) |
 *
 * @client_cpp
 *
 */

/**
 * @brief Per-chip SoC descriptor with coordinate translation and harvesting awareness.
 */
class SocDescriptor {
public:
    /** @name Construction */
    /** @{ */

    /**
     * @brief Constructs a per-chip descriptor from static topology and runtime chip info.
     * @param arch_desc The shared architecture descriptor.
     * @param chip_info Per-chip runtime state (harvesting masks, NOC translation, board identity).
     */
    SocDescriptor(std::shared_ptr<const SocArchDescriptor> arch_desc, const ChipInfo chip_info = {});

    /** @} */

    /** @name Static Helpers */
    /** @{ */

    /**
     * @brief Reads the architecture from a YAML descriptor file without full construction.
     * @param soc_descriptor_path Path to the YAML file.
     */
    static ARCH get_arch_from_soc_descriptor_path(const std::string& soc_descriptor_path);

    /**
     * @brief Reads the grid size from a YAML descriptor file without full construction.
     * @param soc_descriptor_path Path to the YAML file.
     */
    static tt_xy_pair get_grid_size_from_soc_descriptor_path(const std::string& soc_descriptor_path);

    /** @} */

    /**
     * @brief Returns the underlying static architecture descriptor.
     */
    const SocArchDescriptor& get_arch_descriptor() const;

    /** @name Coordinate Translation */
    /** @{ */

    /**
     * @brief Translates a core coordinate to the target coordinate system.
     * @param core_coord Source coordinate.
     * @param coord_system Target coordinate system.
     */
    CoreCoord translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const;

    /**
     * @brief Translates a set of core coordinates to the target coordinate system.
     */
    std::unordered_set<CoreCoord> translate_coords_to(
        const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const;

    /**
     * @brief Translates a set of core coordinates to raw xy pairs in the target system.
     */
    std::unordered_set<tt_xy_pair> translate_coords_to_xy_pair(
        const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const;

    /**
     * @brief Returns the typed coordinate for a raw xy pair in the given coordinate system.
     */
    CoreCoord get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const;

    /**
     * @brief Translates a raw xy pair from one coordinate system to another.
     */
    CoreCoord translate_coord_to(
        const tt_xy_pair core_location,
        const CoordSystem input_coord_system,
        const CoordSystem target_coord_system) const;

    /**
     * @brief Translates a core coordinate to translated xy pair.
     */
    tt_xy_pair translate_chip_coord_to_translated(const CoreCoord core) const;

    /**
     * @brief Translates a core coordinate to translated coordinate system.
     */
    CoreCoord translate_chip_coord_to_translated_coord(const CoreCoord core) const;

    /** @} */

    /** @name Serialization */
    /** @{ */

    /**
     * @brief Serializes the descriptor to a YAML string.
     */
    std::string serialize() const;

    /**
     * @brief Serializes the descriptor to a YAML file.
     * @param dest_file Destination path. Uses a default temp path if empty.
     */
    std::filesystem::path serialize_to_file(const std::filesystem::path& dest_file = "") const;

    /** @} */

    /** @name Core Queries */
    /** @{ */

    /**
     * @brief Returns active (non-harvested) cores of the given type.
     * @param core_type Functional core type to query.
     * @param coord_system Coordinate system for the result.
     * @param channel Optional channel filter (DRAM/ETH).
     */
    std::vector<CoreCoord> get_cores(
        const CoreType core_type,
        const CoordSystem coord_system = CoordSystem::NOC0,
        std::optional<uint32_t> channel = std::nullopt) const;

    /**
     * @brief Returns harvested (disabled) cores of the given type.
     */
    std::vector<CoreCoord> get_harvested_cores(
        const CoreType core_type, const CoordSystem coord_system = CoordSystem::NOC0) const;

    /**
     * @brief Returns all active cores across all types.
     */
    std::vector<CoreCoord> get_all_cores(const CoordSystem coord_system = CoordSystem::NOC0) const;

    /**
     * @brief Returns all harvested cores across all types.
     */
    std::vector<CoreCoord> get_all_harvested_cores(const CoordSystem coord_system = CoordSystem::NOC0) const;

    /**
     * @brief Returns the active grid size for the given core type.
     */
    tt_xy_pair get_grid_size(const CoreType core_type) const;

    /**
     * @brief Returns the harvested grid size for the given core type.
     */
    tt_xy_pair get_harvested_grid_size(const CoreType core_type) const;

    /** @} */

    /** @name DRAM */
    /** @{ */

    /**
     * @brief Returns DRAM core coordinates grouped by channel then subchannel.
     */
    std::vector<std::vector<CoreCoord>> get_dram_cores() const;

    /**
     * @brief Returns the number of active DRAM channels.
     */
    int get_num_dram_channels() const;

    /**
     * @brief Returns the core coordinate for a specific DRAM channel and subchannel.
     */
    CoreCoord get_dram_core_for_channel(
        int dram_chan, int subchannel, const CoordSystem coord_system = CoordSystem::NOC0) const;

    /**
     * @brief Returns the channel and subchannel for a given DRAM core coordinate.
     */
    std::pair<int, int> get_dram_channel_for_core(
        const CoreCoord& core_coord, const CoordSystem coord_system = CoordSystem::NOC0) const;

    /** @} */

    /** @name Ethernet */
    /** @{ */

    /**
     * @brief Returns the number of active Ethernet channels.
     */
    uint32_t get_num_eth_channels() const;

    /**
     * @brief Returns the number of harvested Ethernet channels.
     */
    uint32_t get_num_harvested_eth_channels() const;

    /**
     * @brief Returns the core coordinate for a specific Ethernet channel.
     */
    CoreCoord get_eth_core_for_channel(uint32_t eth_chan, const CoordSystem coord_system = CoordSystem::NOC0) const;

    /**
     * @brief Returns core coordinates for a set of Ethernet channels.
     */
    std::unordered_set<CoreCoord> get_eth_cores_for_channels(
        const std::set<uint32_t>& eth_channels, const CoordSystem coord_system = CoordSystem::NOC0) const;

    /**
     * @brief Returns raw xy pairs for a set of Ethernet channels.
     */
    std::unordered_set<tt_xy_pair> get_eth_xy_pairs_for_channels(
        const std::set<uint32_t>& eth_channels, const CoordSystem coord_system = CoordSystem::NOC0) const;

    /**
     * @brief Returns the Ethernet channel for a given core coordinate.
     */
    uint32_t get_eth_channel_for_core(
        const CoreCoord& core_coord, const CoordSystem coord_system = CoordSystem::NOC0) const;

    /** @} */

    /** @name Architecture Properties (delegated to SocArchDescriptor) */
    /** @{ */

    /**
     * @brief Returns the chip architecture.
     */
    ARCH get_arch() const;

    /**
     * @brief Returns the full NOC grid size (unharvested).
     */
    tt_xy_pair get_grid_size() const;

    /**
     * @brief Returns the L1 memory size per Tensix worker core in bytes.
     */
    int get_worker_l1_size() const;

    /**
     * @brief Returns the L1 memory size per Ethernet core in bytes.
     */
    int get_eth_l1_size() const;

    /**
     * @brief Returns the size of a single DRAM bank in bytes.
     */
    uint64_t get_dram_bank_size() const;

    /**
     * @brief Returns per-TRISC memory region sizes.
     */
    const std::vector<std::size_t>& get_trisc_sizes() const;

    /**
     * @brief Returns the overlay firmware version.
     */
    int get_overlay_version() const;

    /**
     * @brief Returns the unpacker block version.
     */
    int get_unpacker_version() const;

    /**
     * @brief Returns the required destination register size alignment.
     */
    int get_dst_size_alignment() const;

    /**
     * @brief Returns the packer block version.
     */
    int get_packer_version() const;

    /**
     * @brief Returns the path to the source YAML descriptor file.
     */
    const std::string& get_device_descriptor_file_path() const;

    /** @} */

    /** @name Per-Chip Runtime State */
    /** @{ */

    /**
     * @brief Returns whether NOC address translation is active on this chip.
     */
    bool get_noc_translation_enabled() const;

    /**
     * @brief Returns the per-chip harvesting masks in logical coordinates.
     */
    const HarvestingMasks& get_harvesting_masks() const;

    /** @} */

private:
    void init_from_arch_descriptor(const ChipInfo& chip_info);
    void create_coordinate_manager(const BoardType board_type, const uint8_t asic_location);

    static std::filesystem::path get_default_soc_descriptor_file_path();

    void write_coords(void* out, const CoreCoord& core) const;
    void write_core_locations(void* out, const CoreType& core_type) const;
    void serialize_dram_cores(void* out, const std::vector<std::vector<CoreCoord>>& cores) const;

    std::vector<CoreCoord> translate_coordinates(
        const std::vector<CoreCoord>& noc0_cores, const CoordSystem coord_system) const;

    std::shared_ptr<const SocArchDescriptor> arch_desc_;
    std::shared_ptr<CoordinateManager> coordinate_manager = nullptr;

    bool noc_translation_enabled_ = false;
    HarvestingMasks harvesting_masks_ = {};
};

/** @} */  // end of tt_soc_descriptor group

}  // namespace tt::umd
