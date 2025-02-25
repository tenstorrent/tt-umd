/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "umd/device/chip/chip.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_xy_pair.h"
#include "umd/device/types/arch.h"
#include "umd/device/types/cluster_descriptor_types.h"

namespace YAML {
class Node;
}

class tt_ClusterDescriptor {
    friend class tt::umd::Cluster;

private:
    tt_ClusterDescriptor() = default;

    int get_ethernet_link_coord_distance(const eth_coord_t &location_a, const eth_coord_t &location_b) const;

protected:
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
    std::unordered_map<chip_id_t, std::unordered_set<chip_id_t>> chips_grouped_by_closest_mmio;
    std::unordered_map<chip_id_t, tt::ARCH> chip_arch = {};
    std::map<ChipUID, chip_id_t> chip_uid_to_chip_id = {};

    // one-to-many chip connections
    struct Chip2ChipConnection {
        eth_coord_t source_chip_coord;
        std::unordered_set<eth_coord_t> destination_chip_coords;
    };

    // shelf_id -> y dim -> list of chip2chip connections between different shelves
    // assumption is that on every row of the shelf there is a chip that is connected to the other shelf
    // there could be one-to-many connections between shelves, i.e. one chip is connected to multiple chips on the other
    // shelf (in case of nebula->galaxy)
    std::unordered_map<int, std::unordered_map<int, Chip2ChipConnection>> galaxy_shelves_exit_chip_coords_per_y_dim =
        {};
    // rack_id -> x dim -> list of chip2chip connections between different racks
    // assumption is that on every row of the rack there is a chip that is connected to the other rack
    std::unordered_map<int, std::unordered_map<int, Chip2ChipConnection>> galaxy_racks_exit_chip_coords_per_x_dim = {};

    static void load_ethernet_connections_from_connectivity_descriptor(YAML::Node &yaml, tt_ClusterDescriptor &desc);
    static void fill_galaxy_connections(tt_ClusterDescriptor &desc);
    static void load_chips_from_connectivity_descriptor(YAML::Node &yaml, tt_ClusterDescriptor &desc);
    static void merge_cluster_ids(tt_ClusterDescriptor &desc);
    static void load_harvesting_information(YAML::Node &yaml, tt_ClusterDescriptor &desc);

    void fill_chips_grouped_by_closest_mmio();

public:
    /*
     * Returns the pairs of channels that are connected where the first entry in the pair corresponds to the argument
     * ordering when calling the function An empty result implies that the two chips do not share any direct connection
     */
    std::vector<std::tuple<ethernet_channel_t, ethernet_channel_t>>
    get_directly_connected_ethernet_channels_between_chips(const chip_id_t &first, const chip_id_t &second) const;

    bool is_chip_mmio_capable(const chip_id_t chip_id) const;
    bool is_chip_remote(const chip_id_t chip_id) const;
    chip_id_t get_closest_mmio_capable_chip(const chip_id_t chip);
    chip_id_t get_shelf_local_physical_chip_coords(chip_id_t virtual_coord);

    // TODO: These following functions will be removed, and ClusterDescriptor will be created without any parameters.
    // get_cluster_descriptor_file_path will create ethernet map in the background.
    static std::string get_cluster_descriptor_file_path();
    static std::unique_ptr<tt_ClusterDescriptor> create_from_yaml(const std::string &cluster_descriptor_file_path);
    static std::unique_ptr<tt_ClusterDescriptor> create();
    static tt::ARCH detect_arch(const chip_id_t chip_id);

    // This function is used to create mock cluster descriptor yaml files, for example for simulation.
    static std::unique_ptr<tt_ClusterDescriptor> create_mock_cluster(
        const std::vector<chip_id_t> &logical_device_ids, tt::ARCH arch);

    const std::unordered_map<chip_id_t, std::uint32_t> &get_harvesting_info() const;
    const std::unordered_map<chip_id_t, bool> &get_noc_translation_table_en() const;
    const std::unordered_map<chip_id_t, eth_coord_t> &get_chip_locations() const;
    const std::
        unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t>>>
        get_ethernet_connections() const;
    const std::unordered_map<chip_id_t, chip_id_t> get_chips_with_mmio() const;
    const std::unordered_set<chip_id_t> &get_all_chips() const;
    const std::unordered_map<chip_id_t, std::unordered_set<chip_id_t>> &get_chips_grouped_by_closest_mmio() const;
    std::size_t get_number_of_chips() const;

    int get_ethernet_link_distance(chip_id_t chip_a, chip_id_t chip_b) const;

    BoardType get_board_type(chip_id_t chip_id) const;
    tt::ARCH get_arch(chip_id_t chip_id) const;

    chip_id_t get_chip_id(const ChipUID &chip_uid) const;

    bool ethernet_core_has_active_ethernet_link(chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const;
    std::tuple<chip_id_t, ethernet_channel_t> get_chip_and_channel_of_remote_ethernet_core(
        chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const;

    void enable_all_devices();

    std::string serialize() const;

    std::filesystem::path serialize_to_file() const;
};
