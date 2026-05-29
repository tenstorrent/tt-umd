// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <yaml-cpp/node/node.h>

#include <cstdint>
#include <string>
#include <vector>

#include "tt_enums_structs_constants_doxy.hpp"

namespace tt::umd {

/**
 * @defgroup tt_soc_arch_descriptor SocArchDescriptor
 * @{
 *
 * @brief Static, architecture-determined chip topology and constants.
 *
 * SocArchDescriptor captures everything about a chip that is fixed at tapeout:
 * the NOC grid dimensions, every core location, memory sizes, and NOC
 * coordinate mappings. None of this data depends on per-chip runtime state
 * (harvesting masks, NOC translation settings).
 *
 * Shared across all chips of the same architecture. @ref SocDescriptor
 * combines this with per-chip runtime state (harvesting, coordinate translation).
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref CoreDescriptor | Per-core metadata: coordinate, type, L1 size |
 * | @ref SocDescriptor | Per-chip descriptor: wraps SocArchDescriptor + ChipInfo + CoordinateManager |
 *
 * @client_cpp
 *
 */

/**
 * @brief Metadata for a single core on the SoC.
 */
struct CoreDescriptor {
    tt_xy_pair coord = tt_xy_pair(0, 0);  ///< Grid coordinate.
    CoreType type;                        ///< Functional type (TENSIX, DRAM, ETH, etc.).
    std::size_t l1_size = 0;              ///< Local memory size in bytes.
};

/**
 * @brief Static chip topology, identical for every chip of a given architecture.
 *
 * Constructed once from either an architecture enum (hardcoded constants) or a
 * YAML SoC descriptor file (custom/test configurations). All core locations
 * represent the full unharvested floorplan.
 *
 * @client_cpp
 */
class SocArchDescriptor {
public:
    virtual ~SocArchDescriptor() = default;

    /**
     * @brief Populates the descriptor with architecture-specific topology data.
     */
    virtual void init() = 0;

    /** @name Construction */
    /** @{ */

    /**
     * @brief Creates a descriptor from hardcoded architecture constants.
     * @param arch Target architecture.
     */
    SocArchDescriptor(ARCH arch);

    /**
     * @brief Creates a descriptor by parsing a YAML SoC descriptor file.
     *
     * Populates core locations, memory sizes, feature versions, and TRISC sizes
     * from the file. Used for custom or test configurations.
     *
     * @param soc_descriptor_path Path to the YAML file.
     */
    SocArchDescriptor(const std::string& soc_descriptor_path);

    /** @} */

    /** @name Static Helpers */
    /** @{ */

    /**
     * @brief Reads the architecture enum from a YAML descriptor without full construction.
     * @param soc_descriptor_path Path to the YAML file.
     * @return tt::ARCH The architecture found in the file.
     */
    static ARCH get_arch_from_path(const std::string& soc_descriptor_path);

    /**
     * @brief Reads the NOC grid size from a YAML descriptor without full construction.
     * @param soc_descriptor_path Path to the YAML file.
     * @return tt_xy_pair The grid dimensions.
     */
    static tt_xy_pair get_grid_size_from_path(const std::string& soc_descriptor_path);

    /**
     * @brief Computes the bounding grid size from a list of core coordinates.
     * @param cores Core locations to compute the bounding box from.
     * @return tt_xy_pair The bounding grid dimensions.
     */
    static tt_xy_pair calculate_grid_size(const std::vector<tt_xy_pair>& cores);

    /** @} */

    /** @name Architecture Identity */
    /** @{ */

    /**
     * @brief Returns the chip architecture.
     */
    ARCH get_arch() const { return arch_; }

    /**
     * @brief Returns the full NOC grid size (unharvested).
     */
    const tt_xy_pair& get_grid_size() const { return grid_size_; }

    /** @} */

    /** @name Core Locations (unharvested) */
    /** @{ */

    /**
     * @brief Returns all Tensix compute core locations.
     */
    const std::vector<tt_xy_pair>& get_tensix_cores() const { return tensix_cores_; }

    /**
     * @brief Returns DRAM core locations grouped by channel then subchannel.
     *
     * Outer vector index = channel, inner vector index = subchannel within that channel.
     */
    const std::vector<std::vector<tt_xy_pair>>& get_dram_cores() const { return dram_cores_; }

    /**
     * @brief Returns all Ethernet core locations.
     */
    const std::vector<tt_xy_pair>& get_eth_cores() const { return eth_cores_; }

    /**
     * @brief Returns all firmware core locations.
     */
    const std::vector<tt_xy_pair>& get_firmware_cores() const { return firmware_cores_; }

    /**
     * @brief Returns all PCIe tile locations.
     */
    const std::vector<tt_xy_pair>& get_pcie_cores() const { return pcie_cores_; }

    /**
     * @brief Returns all NOC router core locations.
     */
    const std::vector<tt_xy_pair>& get_router_cores() const { return router_cores_; }

    /**
     * @brief Returns all security core locations.
     */
    const std::vector<tt_xy_pair>& get_security_cores() const { return security_cores_; }

    /**
     * @brief Returns all L2 CPU core locations.
     */
    const std::vector<tt_xy_pair>& get_l2cpu_cores() const { return l2cpu_cores_; }

    /**
     * @brief Returns all dispatch core locations.
     */
    const std::vector<tt_xy_pair>& get_dispatch_cores() const { return dispatch_cores_; }

    /** @} */

    /** @name Memory Sizes */
    /** @{ */

    /**
     * @brief Returns the L1 memory size per Tensix worker core in bytes.
     */
    uint32_t get_worker_l1_size() const { return worker_l1_size_; }

    /**
     * @brief Returns the L1 memory size per Ethernet core in bytes.
     */
    uint32_t get_eth_l1_size() const { return eth_l1_size_; }

    /**
     * @brief Returns the size of a single DRAM bank in bytes.
     */
    uint64_t get_dram_bank_size() const { return dram_bank_size_; }

    /** @} */

    /** @name NOC Coordinate Mapping */
    /** @{ */

    /**
     * @brief Returns the X-axis coordinate translation table between the two NOCs.
     *
     * Static property of the grid wiring, independent of the per-chip NOC
     * translation setting on @ref SocDescriptor.
     */
    const std::vector<uint32_t>& get_noc0_x_to_noc1_x() const { return noc0_x_to_noc1_x_; }

    /**
     * @brief Returns the Y-axis coordinate translation table between the two NOCs.
     * @see get_noc0_x_to_noc1_x()
     */
    const std::vector<uint32_t>& get_noc0_y_to_noc1_y() const { return noc0_y_to_noc1_y_; }

    /** @} */

    /** @name Feature Versions */
    /** @{ */

    /**
     * @brief Returns the overlay firmware version. Zero when constructed from arch enum.
     */
    int get_overlay_version() const { return overlay_version_; }

    /**
     * @brief Returns the unpacker block version. Zero when constructed from arch enum.
     */
    int get_unpacker_version() const { return unpacker_version_; }

    /**
     * @brief Returns the required destination register size alignment.
     */
    int get_dst_size_alignment() const { return dst_size_alignment_; }

    /**
     * @brief Returns the packer block version. Zero when constructed from arch enum.
     */
    int get_packer_version() const { return packer_version_; }

    /**
     * @brief Returns per-TRISC memory region sizes. Empty when constructed from arch enum.
     */
    const std::vector<std::size_t>& get_trisc_sizes() const { return trisc_sizes_; }

    /** @} */

    /**
     * @brief Returns the path to the source YAML file. Empty when constructed from arch enum.
     */
    const std::string& get_device_descriptor_file_path() const { return device_descriptor_file_path_; }

    /** @name Derived Data */
    /** @{ */

    /**
     * @brief Returns the full core descriptor map (coordinate → CoreDescriptor).
     *
     * Built at construction from all core location vectors. Every core on the SoC
     * has an entry regardless of type.
     */
    const std::unordered_map<tt_xy_pair, CoreDescriptor>& get_cores() const { return cores_; }

    /**
     * @brief Returns the Tensix worker grid dimensions (unharvested).
     *
     * Computed from the bounding box of Tensix core locations. This is the full
     * architectural grid before harvesting is applied by CoordinateManager.
     */
    const tt_xy_pair& get_worker_grid_size() const { return worker_grid_size_; }

    /**
     * @brief Returns the DRAM core-to-channel mapping (coordinate → channel, subchannel).
     */
    const std::unordered_map<tt_xy_pair, std::tuple<int, int>>& get_dram_core_channel_map() const {
        return dram_core_channel_map_;
    }

    /**
     * @brief Returns the Ethernet core-to-channel mapping (coordinate → channel index).
     */
    const std::unordered_map<tt_xy_pair, int>& get_ethernet_core_channel_map() const {
        return ethernet_core_channel_map_;
    }

    /** @} */

private:
    SocArchDescriptor() = default;

    void build_derived_data();
    void load_from_yaml(YAML::Node& device_descriptor_yaml);

    static std::vector<tt_xy_pair> convert_to_tt_xy_pair(const std::vector<std::string>& core_strings);
    static std::vector<std::vector<tt_xy_pair>> convert_dram_cores_from_yaml(
        YAML::Node& device_descriptor_yaml, const std::string& dram_core = "dram");

    ARCH arch_;
    tt_xy_pair grid_size_;
    std::vector<tt_xy_pair> tensix_cores_;
    std::vector<std::vector<tt_xy_pair>> dram_cores_;
    std::vector<tt_xy_pair> eth_cores_;
    std::vector<tt_xy_pair> firmware_cores_;
    std::vector<tt_xy_pair> pcie_cores_;
    std::vector<tt_xy_pair> router_cores_;
    std::vector<tt_xy_pair> security_cores_;
    std::vector<tt_xy_pair> l2cpu_cores_;
    std::vector<tt_xy_pair> dispatch_cores_;
    uint32_t worker_l1_size_ = 0;
    uint32_t eth_l1_size_ = 0;
    uint64_t dram_bank_size_ = 0;
    std::vector<uint32_t> noc0_x_to_noc1_x_;
    std::vector<uint32_t> noc0_y_to_noc1_y_;
    int overlay_version_ = 0;
    int unpacker_version_ = 0;
    int dst_size_alignment_ = 0;
    int packer_version_ = 0;
    std::vector<std::size_t> trisc_sizes_;
    std::string device_descriptor_file_path_;
    std::unordered_map<tt_xy_pair, CoreDescriptor> cores_;
    tt_xy_pair worker_grid_size_;
    std::unordered_map<tt_xy_pair, std::tuple<int, int>> dram_core_channel_map_;
    std::unordered_map<tt_xy_pair, int> ethernet_core_channel_map_;
};

/** @} */  // end of tt_soc_arch_descriptor group

}  // namespace tt::umd
