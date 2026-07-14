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

#include "umd/device/coordinates/coordinate_manager.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class CoordinateManager;
class SocArchDescriptor;

//! SocDescriptor contains information regarding the SOC configuration targetted.
/*!
    Should only contain relevant configuration for SOC.
*/
class SocDescriptor {
public:
    SocDescriptor(std::shared_ptr<const SocArchDescriptor> arch_desc, const ChipInfo chip_info = {});

    // Helpers for extracting info from soc descriptor file.
    static tt::ARCH get_arch_from_soc_descriptor_path(const std::string& soc_descriptor_path);
    static tt_xy_pair get_grid_size_from_soc_descriptor_path(const std::string& soc_descriptor_path);

    // Access the underlying static architecture descriptor.
    const SocArchDescriptor& get_arch_descriptor() const;

    // CoreCoord conversions.
    CoreCoord translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const;
    std::unordered_set<CoreCoord> translate_coords_to(
        const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const;
    std::unordered_set<tt_xy_pair> translate_coords_to_xy_pair(
        const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const;
    CoreCoord get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const;
    CoreCoord translate_coord_to(
        const tt_xy_pair core_location,
        const CoordSystem input_coord_system,
        const CoordSystem target_coord_system) const;
    tt_xy_pair translate_chip_coord_to_translated(const CoreCoord core) const;
    CoreCoord translate_chip_coord_to_translated_coord(const CoreCoord core) const;

    // Serialize the soc descriptor to a YAML string, or directly to a file.
    // A default file in /tmp directory will be used if no path is passed.
    std::string serialize() const;
    std::filesystem::path serialize_to_file(const std::filesystem::path& dest_file = "") const;

    // Returns true when `core` (given in `coord_system`) matches any core of `core_type`.
    bool is_core_of_type(const tt_xy_pair& core, CoreType core_type, CoordSystem coord_system) const;

    std::vector<CoreCoord> get_cores(
        const CoreType core_type,
        const CoordSystem coord_system = CoordSystem::NOC0,
        std::optional<uint32_t> channel = std::nullopt) const;
    std::vector<CoreCoord> get_harvested_cores(
        const CoreType core_type, const CoordSystem coord_system = CoordSystem::NOC0) const;
    std::vector<CoreCoord> get_all_cores(const CoordSystem coord_system = CoordSystem::NOC0) const;
    std::vector<CoreCoord> get_all_harvested_cores(const CoordSystem coord_system = CoordSystem::NOC0) const;

    tt_xy_pair get_grid_size(const CoreType core_type) const;
    tt_xy_pair get_harvested_grid_size(const CoreType core_type) const;

    std::pair<CoreCoord, CoreCoord> get_bounding_rectangle(
        CoordSystem coord_system = CoordSystem::TRANSLATED, CoreType core_type = CoreType::TENSIX) const;

    std::vector<std::vector<CoreCoord>> get_dram_cores() const;

    int get_num_dram_channels() const;

    uint32_t get_num_eth_channels() const;
    uint32_t get_num_harvested_eth_channels() const;

    // LOGICAL coordinates for DRAM and ETH are tightly coupled with channels, so this code is very similar to what
    // would translate_coord_to do for a coord with LOGICAL coords.
    CoreCoord get_dram_core_for_channel(
        int dram_chan, int subchannel, const CoordSystem coord_system = CoordSystem::NOC0) const;
    CoreCoord get_eth_core_for_channel(uint32_t eth_chan, const CoordSystem coord_system = CoordSystem::NOC0) const;
    std::unordered_set<CoreCoord> get_eth_cores_for_channels(
        const std::set<uint32_t>& eth_channels, const CoordSystem coord_system = CoordSystem::NOC0) const;
    std::unordered_set<tt_xy_pair> get_eth_xy_pairs_for_channels(
        const std::set<uint32_t>& eth_channels, const CoordSystem coord_system = CoordSystem::NOC0) const;
    uint32_t get_eth_channel_for_core(
        const CoreCoord& core_coord, const CoordSystem coord_system = CoordSystem::NOC0) const;
    // First element is the channel, second element is the subchannel.
    std::pair<int, int> get_dram_channel_for_core(
        const CoreCoord& core_coord, const CoordSystem coord_system = CoordSystem::NOC0) const;

    // Public data members kept for backward compatibility (Phase 1).
    // These are copied from the SocArchDescriptor at construction time.
    // In a future phase, they will be replaced by getter methods.
    tt::ARCH arch;
    tt_xy_pair grid_size;
    std::vector<std::size_t> trisc_sizes;  // Most of software stack assumes same trisc size for whole chip.
    std::string device_descriptor_file_path = std::string("");

    int overlay_version;
    int unpacker_version;
    int dst_size_alignment;
    int packer_version;
    int worker_l1_size;
    int eth_l1_size;
    uint64_t dram_bank_size;

    // Passed through constructor.
    bool noc_translation_enabled;

    // Harvesting mask is reported in logical coordinates, meaning the index of a bit that is set corresponds to the
    // index of the TENSIX row (Wormhole) or column (Blackhole), or the index of the DRAM channel, or the index of the
    // ETH channel as reported in the soc descriptor. Examples:
    //   - Tensix harvesting mask "2" would mean the second row/column from soc descriptor is harvested, and not
    //     NOC0 row.
    //   - Eth harvesting mask "2" would mean that the second core in eth_cores in soc descriptor is harvested, which
    //     is the same one that would be reported as channel 1 and would have logical coords (0, 1). This mask doesn't
    //     mean that the second core in NOC0 chain is harvested.
    HarvestingMasks harvesting_masks;

private:
    void init_from_arch_descriptor(const ChipInfo& chip_info);
    void create_coordinate_manager(const BoardType board_type, const uint8_t asic_location);

    static std::filesystem::path get_default_soc_descriptor_file_path();

    // Since including yaml-cpp/yaml.h here breaks metal build we use void* type instead of YAML::Emitter.
    void write_coords(void* out, const CoreCoord& core) const;
    void write_core_locations(void* out, const CoreType& core_type) const;
    void serialize_dram_cores(void* out, const std::vector<std::vector<CoreCoord>>& cores) const;

    std::vector<CoreCoord> translate_coordinates(
        const std::vector<CoreCoord>& noc0_cores, const CoordSystem coord_system) const;

    // The static architecture descriptor (shared across chips of the same arch).
    std::shared_ptr<const SocArchDescriptor> arch_desc_;

    // TODO: change this to unique pointer as soon as copying of SocDescriptor
    // is not needed anymore. Soc descriptor and coordinate manager should be
    // created once per chip.
    std::shared_ptr<CoordinateManager> coordinate_manager = nullptr;
};

}  // namespace tt::umd
