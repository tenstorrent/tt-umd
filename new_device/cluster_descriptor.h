/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <set>

#include "soc_descriptor.h"

namespace YAML { class Node; }

namespace tt::umd {

struct ChipConnection {
    int device_id;
};

enum BoardType : uint32_t {
    N150 = 0,
    N300 = 1,
    GALAXY = 2,
    DEFAULT = 3,
};


class ClusterDescriptor {
   public:
    // Assume cluster doesn't change during the process lifetime.
    static ClusterDescriptor *GetClusterDescriptor(bool create_for_grayskull_cluster = false);

    static std::unique_ptr<ClusterDescriptor> cluster_descriptor;

    static void GenerateClusterMap();

    // brosko: rename to "create for local physical devices"
    static std::unique_ptr<ClusterDescriptor> create_for_grayskull_cluster(
        const std::set<chip_id_t> &logical_mmio_device_ids, const std::vector<chip_id_t> &physical_mmio_device_ids);
    static std::unique_ptr<ClusterDescriptor> create_from_yaml(std::filesystem::path cluster_descriptor_yaml_file);

    ClusterDescriptor() = default;

    std::vector<SocDescriptor> soc_descriptors;

    void load_ethernet_connections_from_connectivity_descriptor(YAML::Node &yaml);
    void load_chips_from_connectivity_descriptor(YAML::Node &yaml);
    void load_harvesting_information(YAML::Node &yaml);

    std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t>>>
        ethernet_connections;
    std::unordered_map<chip_id_t, eth_coord_t> chip_locations;
    // reverse map: rack/shelf/y/x -> chip_id
    std::map<int, std::map<int, std::map<int, std::map<int, chip_id_t>>>> coords_to_chip_ids;
    std::unordered_map<chip_id_t, chip_id_t> chips_with_mmio;
    std::unordered_set<chip_id_t> all_chips;
    std::unordered_map<chip_id_t, bool> noc_translation_enabled = {};
    std::unordered_map<chip_id_t, std::uint32_t> harvesting_masks = {};
    std::unordered_set<chip_id_t> enabled_active_chips;
    std::unordered_map<chip_id_t, chip_id_t> closest_mmio_chip_cache = {};
    std::unordered_map<chip_id_t, BoardType> chip_board_type = {};

    /*
     * Returns the pairs of channels that are connected where the first entry in the pair corresponds to the argument
     * ordering when calling the function An empty result implies that the two chips do not share any direct connection
     */
    std::vector<std::tuple<ethernet_channel_t, ethernet_channel_t>>
    get_directly_connected_ethernet_channels_between_chips(const chip_id_t &first, const chip_id_t &second) const;

    bool is_chip_mmio_capable(const chip_id_t &chip_id) const;
    chip_id_t get_closest_mmio_capable_chip(const chip_id_t &chip);
    chip_id_t get_shelf_local_physical_chip_coords(chip_id_t virtual_coord);

    std::unordered_map<chip_id_t, std::uint32_t> get_harvesting_info() const;
    std::unordered_map<chip_id_t, bool> get_noc_translation_table_en() const;
    std::unordered_map<chip_id_t, eth_coord_t> get_chip_locations() const;
    std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t>>>
    get_ethernet_connections() const;
    std::unordered_map<chip_id_t, chip_id_t> get_chips_with_mmio() const;
    std::unordered_set<chip_id_t> get_all_chips() const;
    std::size_t get_number_of_chips() const;

    int get_ethernet_link_distance(chip_id_t chip_a, chip_id_t chip_b) const;

    BoardType get_board_type(chip_id_t chip_id) const;

    bool ethernet_core_has_active_ethernet_link(chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const;
    std::tuple<chip_id_t, ethernet_channel_t> get_chip_and_channel_of_remote_ethernet_core(
        chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const;

    void enable_all_devices();

    int get_ethernet_link_coord_distance(const eth_coord_t &location_a, const eth_coord_t &location_b) const;
};

}  // namespace tt::umd