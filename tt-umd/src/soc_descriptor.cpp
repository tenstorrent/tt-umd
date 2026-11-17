// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/soc_descriptor.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

SocDescriptor::SocDescriptor(std::shared_ptr<const SocArchDescriptor> arch_desc, const ChipInfo chip_info) {
    UMD_NOT_IMPLEMENTED();
}

tt::ARCH SocDescriptor::get_arch_from_soc_descriptor_path(const std::string& soc_descriptor_path) {
    UMD_NOT_IMPLEMENTED();
}

tt_xy_pair SocDescriptor::get_grid_size_from_soc_descriptor_path(const std::string& soc_descriptor_path) {
    UMD_NOT_IMPLEMENTED();
}

const SocArchDescriptor& SocDescriptor::get_arch_descriptor() const { UMD_NOT_IMPLEMENTED(); }

CoreCoord SocDescriptor::translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::unordered_set<CoreCoord> SocDescriptor::translate_coords_to(
    const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::unordered_set<tt_xy_pair> SocDescriptor::translate_coords_to_xy_pair(
    const std::unordered_set<CoreCoord>& core_coord, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

CoreCoord SocDescriptor::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

CoreCoord SocDescriptor::translate_coord_to(
    const tt_xy_pair core_location, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

tt_xy_pair SocDescriptor::translate_chip_coord_to_translated(const CoreCoord core, const NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

CoreCoord SocDescriptor::translate_chip_coord_to_translated_coord(const CoreCoord core, const NocId noc_id) const {
    UMD_NOT_IMPLEMENTED();
}

std::string SocDescriptor::serialize() const { UMD_NOT_IMPLEMENTED(); }

std::filesystem::path SocDescriptor::serialize_to_file(const std::filesystem::path& dest_file) const {
    UMD_NOT_IMPLEMENTED();
}

bool SocDescriptor::is_core_of_type(const tt_xy_pair& core, CoreType core_type, CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<CoreCoord> SocDescriptor::get_cores(
    const CoreType core_type, const CoordSystem coord_system, std::optional<uint32_t> channel) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<CoreCoord> SocDescriptor::get_harvested_cores(
    const CoreType core_type, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<CoreCoord> SocDescriptor::get_all_cores(const CoordSystem coord_system) const { UMD_NOT_IMPLEMENTED(); }

std::vector<CoreCoord> SocDescriptor::get_all_harvested_cores(const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

tt_xy_pair SocDescriptor::get_grid_size(const CoreType core_type) const { UMD_NOT_IMPLEMENTED(); }

tt_xy_pair SocDescriptor::get_harvested_grid_size(const CoreType core_type) const { UMD_NOT_IMPLEMENTED(); }

std::pair<CoreCoord, CoreCoord> SocDescriptor::get_bounding_rectangle(
    CoordSystem coord_system, CoreType core_type) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<std::vector<CoreCoord>> SocDescriptor::get_dram_cores() const { UMD_NOT_IMPLEMENTED(); }

int SocDescriptor::get_num_dram_channels() const { UMD_NOT_IMPLEMENTED(); }

uint32_t SocDescriptor::get_num_eth_channels() const { UMD_NOT_IMPLEMENTED(); }

uint32_t SocDescriptor::get_num_harvested_eth_channels() const { UMD_NOT_IMPLEMENTED(); }

CoreCoord SocDescriptor::get_dram_core_for_channel(
    int dram_chan, int subchannel, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

CoreCoord SocDescriptor::get_eth_core_for_channel(uint32_t eth_chan, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::unordered_set<CoreCoord> SocDescriptor::get_eth_cores_for_channels(
    const std::set<uint32_t>& eth_channels, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::unordered_set<tt_xy_pair> SocDescriptor::get_eth_xy_pairs_for_channels(
    const std::set<uint32_t>& eth_channels, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

uint32_t SocDescriptor::get_eth_channel_for_core(const CoreCoord& core_coord, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

std::pair<int, int> SocDescriptor::get_dram_channel_for_core(
    const CoreCoord& core_coord, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

void SocDescriptor::init_from_arch_descriptor(const ChipInfo& chip_info) { UMD_NOT_IMPLEMENTED(); }

void SocDescriptor::create_coordinate_manager(const BoardType board_type, const uint8_t asic_location) {
    UMD_NOT_IMPLEMENTED();
}

std::filesystem::path SocDescriptor::get_default_soc_descriptor_file_path() { UMD_NOT_IMPLEMENTED(); }

void SocDescriptor::write_coords(void* out, const CoreCoord& core) const { UMD_NOT_IMPLEMENTED(); }

void SocDescriptor::write_core_locations(void* out, const CoreType& core_type) const { UMD_NOT_IMPLEMENTED(); }

void SocDescriptor::serialize_dram_cores(void* out, const std::vector<std::vector<CoreCoord>>& cores) const {
    UMD_NOT_IMPLEMENTED();
}

std::vector<CoreCoord> SocDescriptor::translate_coordinates(
    const std::vector<CoreCoord>& noc0_cores, const CoordSystem coord_system) const {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
