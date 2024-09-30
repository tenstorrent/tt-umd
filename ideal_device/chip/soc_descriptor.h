/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "common_types.h"

#include <unordered_map>
#include <vector>

namespace tt::umd {

//! SocCore type enumerations
/*! Superset for all chip generations */
enum class CoreType {
    ARC,
    DRAM,
    ETH,
    PCIE,
    WORKER,
    HARVESTED,
    ROUTER_ONLY,
};

// CoreDescriptor class probably not needed.
struct CoreDescriptor {
  xy_pair coord = xy_pair(0, 0);
  CoreType type;

  std::size_t l1_size = 0;
};

class logical_coord : public xy_pair {};
class physical_coord : public xy_pair {};
class harvested_coord : public xy_pair {};

// Holds all physical layout info of the chip
// There are a couple of different types of coordinates
// Logical coordinates:
//   - Worker and eth cores: They use x,y, and go from 0,0 to grid_size.x, grid_size.y
//   - DRAM: They use ch,subch and go from 0 to num_dram_banks, num_dram_subchannels
// DOUBLE CHECK DO WE HAVE HARDWARE HARVESTING
// Physical coordinates:
//   - They resolve to an actual physical location on the chip, if the chip was not harvested
//   - They are coordinates which you should give to NOC0 for routing.
//   - Both logical (worker and dram) coords resolve to the same format of physical coords
//   - They use x,y and go from 0,0 to physical_grid_size.x, physical_grid_size.y
//   - You can ask for NOC1 physical coords from NOC0 physical coords
// Harvested coordinates:
//   - The actual physical location of the core on the harvested chip.
//   - This is abstracted away from the user.
//   - Only with this coords you can hit a core that was harvested.

// TODO take more stuff from metal_SocDescriptor
class SocDescriptor {
   public:
    // To end up at the right layout, we need to load the default soc descriptor and then perform harvesting
    SocDescriptor(std::string device_descriptor_path);
    void perform_harvesting(uint32_t harvesting_mask);

    physical_coord get_physical_from_logical(logical_coord core);
    logical_coord get_logical_from_physical(physical_coord core);
    physical_coord get_physical_from_logical_dram(int channel, int subchannel);
    std::pair<int,int> get_logical_dram_from_physical(physical_coord core);

    bool is_harvested_core(const harvested_coord& core) const;
    CoreType get_core_type(xy_pair core) const;

    tt::umd::Arch arch;
    xy_pair grid_size;
    xy_pair physical_grid_size;
    xy_pair worker_grid_size;
    std::unordered_map<xy_pair, CoreDescriptor> cores;
    std::vector<xy_pair> arc_cores;
    std::vector<xy_pair> workers;
    std::vector<xy_pair> harvested_workers;
    std::vector<xy_pair> pcie_cores;
    std::unordered_map<int, int> worker_log_to_routing_x;
    std::unordered_map<int, int> worker_log_to_routing_y;
    std::unordered_map<int, int> routing_x_to_worker_x;
    std::unordered_map<int, int> routing_y_to_worker_y;
    std::vector<std::vector<xy_pair>> dram_cores;                             // per channel list of dram cores
    std::unordered_map<xy_pair, std::tuple<int, int>> dram_core_channel_map;  // map dram core to chan/subchan
    std::vector<xy_pair> ethernet_cores;                                      // ethernet cores (index == channel id)
    std::unordered_map<xy_pair, int> ethernet_core_channel_map;
    std::vector<std::size_t> trisc_sizes;  // Most of software stack assumes same trisc size for whole chip..
    std::string device_descriptor_file_path = std::string("");
    bool has(xy_pair input) { return cores.find(input) != cores.end(); }
    int overlay_version;
    int unpacker_version;
    int dst_size_alignment;
    int packer_version;
    int worker_l1_size;
    int eth_l1_size;
    bool noc_translation_id_enabled;
    uint64_t dram_bank_size;
};

}  // namespace tt::umd