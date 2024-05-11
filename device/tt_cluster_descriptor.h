/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#pragma once

#include "device/tt_xy_pair.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <tuple>
#include <string>
#include <vector>
#include <memory>
#include "device/tt_cluster_descriptor_types.h"

namespace YAML { class Node; }

enum BoardType : uint32_t {
    N150 = 0,
    N300 = 1,
    GALAXY = 2,
    DEFAULT = 3,
};

class tt_ClusterDescriptor {

  private:
  int get_ethernet_link_coord_distance(const eth_coord_t &location_a, const eth_coord_t &location_b) const;

  protected:

  std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t> > > ethernet_connections;
  std::unordered_map<chip_id_t, eth_coord_t> chip_locations;
  // reverse map: rack/shelf/y/x -> chip_id
  std::map<int, std::map<int, std::map<int, std::map<int, chip_id_t > > > > coords_to_chip_ids;
  std::unordered_map<chip_id_t, chip_id_t> chips_with_mmio;
  std::unordered_set<chip_id_t> all_chips;
  std::unordered_map<chip_id_t, bool> noc_translation_enabled = {};
  std::unordered_map<chip_id_t, std::uint32_t> harvesting_masks = {};
  std::unordered_set<chip_id_t> enabled_active_chips;
  std::unordered_map<chip_id_t, chip_id_t> closest_mmio_chip_cache = {};
  std::unordered_map<chip_id_t, BoardType> chip_board_type = {};

  // one-to-many chip connections
  struct Chip2ChipConnection {
    eth_coord_t source_chip_coord;
    std::unordered_set<eth_coord_t> destination_chip_coords;
  };

  // shelf_id -> y dim -> list of chip2chip connections between different shelves
  // assumption is that on every row of the shelf there is a chip that is connected to the other shelf
  // there could be one-to-many connections between shelves, i.e. one chip is connected to multiple chips on the other shelf (in case of nebula->galaxy)
  std::unordered_map<int, std::unordered_map<int, Chip2ChipConnection > > galaxy_shelves_exit_chip_coords_per_y_dim = {};
  // rack_id -> x dim -> list of chip2chip connections between different racks
  // assumption is that on every row of the rack there is a chip that is connected to the other rack
  std::unordered_map<int, std::unordered_map<int, Chip2ChipConnection > > galaxy_racks_exit_chip_coords_per_x_dim = {};

  static void load_ethernet_connections_from_connectivity_descriptor(YAML::Node &yaml, tt_ClusterDescriptor &desc);
  static void load_chips_from_connectivity_descriptor(YAML::Node &yaml, tt_ClusterDescriptor &desc);
  static void load_harvesting_information(YAML::Node &yaml, tt_ClusterDescriptor &desc);

 public:
  tt_ClusterDescriptor() = default;
  tt_ClusterDescriptor(const tt_ClusterDescriptor&) = default;

  /*
   * Returns the pairs of channels that are connected where the first entry in the pair corresponds to the argument ordering when calling the function
   * An empty result implies that the two chips do not share any direct connection
   */
  std::vector<std::tuple<ethernet_channel_t, ethernet_channel_t>> get_directly_connected_ethernet_channels_between_chips(const chip_id_t &first, const chip_id_t &second) const;
  
  bool channels_are_directly_connected(const chip_id_t &first, const ethernet_channel_t &first_channel, const chip_id_t &second, const ethernet_channel_t &second_channel) const;
  bool is_chip_mmio_capable(const chip_id_t &chip_id) const;
  chip_id_t get_closest_mmio_capable_chip(const chip_id_t &chip);
  chip_id_t get_shelf_local_physical_chip_coords(chip_id_t virtual_coord);
  static std::unique_ptr<tt_ClusterDescriptor> create_from_yaml(const std::string &cluster_descriptor_file_path);
  static std::unique_ptr<tt_ClusterDescriptor> create_for_grayskull_cluster(
      const std::set<chip_id_t> &logical_mmio_device_ids,
      const std::vector<chip_id_t> &physical_mmio_device_ids);
  // const eth_coord_t get_chip_xy(const chip_id_t &chip_id) const;
  // const chip_id_t get_chip_id_at_location(const eth_coord_t &chip_location) const;

  bool chips_have_ethernet_connectivity() const;
  std::unordered_map<chip_id_t, std::uint32_t> get_harvesting_info() const;
  std::unordered_map<chip_id_t, bool> get_noc_translation_table_en() const;
  std::unordered_map<chip_id_t, eth_coord_t> get_chip_locations() const;
  std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t> > > get_ethernet_connections() const;
  std::unordered_map<chip_id_t, chip_id_t> get_chips_with_mmio() const;
  std::unordered_set<chip_id_t> get_all_chips() const;
  std::size_t get_number_of_chips() const;

  int get_ethernet_link_distance(chip_id_t chip_a, chip_id_t chip_b) const;

  BoardType get_board_type(chip_id_t chip_id) const;

  bool ethernet_core_has_active_ethernet_link(chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const;
  std::tuple<chip_id_t, ethernet_channel_t> get_chip_and_channel_of_remote_ethernet_core(chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const;

  void specify_enabled_devices(const std::vector<chip_id_t> &chip_ids);
  void enable_all_devices();

};

std::set<chip_id_t> get_sequential_chip_id_set(int num_chips);
