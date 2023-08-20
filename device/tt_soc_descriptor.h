
#pragma once

#include <cstddef>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>

#include <iostream>
#include <string>

#include "tt_xy_pair.h"
#include "device/tt_arch_types.h"

static constexpr std::size_t DEFAULT_L1_SIZE = 1 * 1024 * 1024;
static constexpr std::size_t DEFAULT_DRAM_SIZE_PER_CORE = 8 * 1024 * 1024;
static constexpr int NUM_TRISC = 3;

static constexpr tt::ARCH ArchNameDefault = tt::ARCH::GRAYSKULL;

std::ostream &operator<<(std::ostream &out, const tt::ARCH &arch_name);

static inline std::string get_arch_str(const tt::ARCH arch_name){
    std::string arch_name_str;

    if (arch_name == tt::ARCH::JAWBRIDGE) {
        arch_name_str = "jawbridge";
    } else if (arch_name == tt::ARCH::GRAYSKULL) {
        arch_name_str = "grayskull";
    } else if (arch_name == tt::ARCH::WORMHOLE) {
        arch_name_str = "wormhole";
    } else if (arch_name == tt::ARCH::WORMHOLE_B0) {
        arch_name_str = "wormhole_b0";
    } else {
        throw std::runtime_error("Invalid arch_name");
    }

    return arch_name_str;
}

static inline tt::ARCH get_arch_name(const std::string &arch_str){
    tt::ARCH arch;

    if ((arch_str == "jawbridge") || (arch_str == "JAWBRIDGE")) {
        arch = tt::ARCH::JAWBRIDGE;
    } else if ((arch_str == "grayskull") || (arch_str == "GRAYSKULL")) {
        arch = tt::ARCH::GRAYSKULL;
    } else if ((arch_str == "wormhole") || (arch_str == "WORMHOLE")){
        arch = tt::ARCH::WORMHOLE;
    } else if ((arch_str == "wormhole_b0") || (arch_str == "WORMHOLE_B0")){
        arch = tt::ARCH::WORMHOLE_B0;
    }else {
        throw std::runtime_error(
            "At LoadSocDescriptorFromYaml: \"" + arch_str + "\" is not recognized as tt::ARCH.");
    }

    return arch;
}

std::string format_node(tt_xy_pair xy);

tt_xy_pair format_node(std::string str);

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

//! SocNodeDescriptor contains information regarding the Node/Core
/*!
    Should only contain relevant configuration for SOC
*/
struct CoreDescriptor {
  tt_xy_pair coord = tt_xy_pair(0, 0);
  CoreType type;

  std::size_t l1_size = 0;
  std::size_t dram_size_per_core = 0;
};

//! tt_SocDescriptor contains information regarding the SOC configuration targetted.
/*!
    Should only contain relevant configuration for SOC
*/
struct tt_SocDescriptor {
  tt::ARCH arch;
  tt_xy_pair grid_size;
  tt_xy_pair physical_grid_size;
  tt_xy_pair worker_grid_size;
  std::unordered_map<tt_xy_pair, CoreDescriptor> cores;
  std::vector<tt_xy_pair> arc_cores;
  std::vector<tt_xy_pair> workers;
  std::vector<tt_xy_pair> harvested_workers;
  std::vector<tt_xy_pair> pcie_cores;
  std::unordered_map<int, int> worker_log_to_routing_x;
  std::unordered_map<int, int> worker_log_to_routing_y;
  std::unordered_map<int, int> routing_x_to_worker_x;
  std::unordered_map<int, int> routing_y_to_worker_y;
  std::vector<std::vector<tt_xy_pair>> dram_cores;  // per channel list of dram cores
  std::unordered_map<tt_xy_pair, std::tuple<int, int>> dram_core_channel_map;  // map dram core to chan/subchan
  std::vector<tt_xy_pair> ethernet_cores;  // ethernet cores (index == channel id)
  std::unordered_map<tt_xy_pair,int> ethernet_core_channel_map;
  std::vector<std::size_t> trisc_sizes;  // Most of software stack assumes same trisc size for whole chip..
  std::string device_descriptor_file_path = std::string("");
  bool has(tt_xy_pair input) { return cores.find(input) != cores.end(); }
  int overlay_version;
  int unpacker_version;
  int dst_size_alignment;
  int packer_version;
  int worker_l1_size;
  int eth_l1_size;
  bool noc_translation_id_enabled;
  uint32_t dram_bank_size;
  std::unordered_map<tt_xy_pair, std::vector<tt_xy_pair>> perf_dram_bank_to_workers;

  int get_num_dram_channels() const;
  std::vector<int> get_dram_chan_map();
  bool is_worker_core(const tt_xy_pair &core) const;
  tt_xy_pair get_worker_core(const tt_xy_pair& core) const;
  tt_xy_pair get_routing_core(const tt_xy_pair& core) const;
  tt_xy_pair get_core_for_dram_channel(int dram_chan, int subchannel) const;
  tt_xy_pair get_pcie_core(int pcie_id = 0) const;
  bool is_ethernet_core(const tt_xy_pair &core) const;
  int get_channel_of_ethernet_core(const tt_xy_pair &core) const;
  int get_num_dram_subchans() const;
  int get_num_dram_blocks_per_channel() const;
  uint64_t get_noc2host_offset(uint16_t host_channel) const;
};

// Allocates a new soc descriptor on the heap. Returns an owning pointer.
// std::unique_ptr<tt_SocDescriptor> load_soc_descriptor_from_yaml(std::string device_descriptor_file_path);
