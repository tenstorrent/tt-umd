/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <unordered_map>
#include <vector>

#include "common_types.h"

namespace YAML {
    class Node;
}

namespace tt::umd {


std::string format_node(xy_pair xy);

xy_pair format_node(std::string str);

// brosko: move to socdescriptor directly. also see where used and if THAT can be moved to socdescriptor
static inline std::string get_arch_str(const Arch arch_name){
    std::string arch_name_str;

    if (arch_name == Arch::JAWBRIDGE) {
        arch_name_str = "jawbridge";
    } else if (arch_name == Arch::GRAYSKULL) {
        arch_name_str = "grayskull";
    } else if (arch_name == Arch::WORMHOLE) {
        arch_name_str = "wormhole";
    } else if (arch_name == Arch::WORMHOLE_B0) {
        arch_name_str = "wormhole_b0";
    } else if (arch_name == Arch::BLACKHOLE) {
        arch_name_str = "blackhole";
    } else {
        throw std::runtime_error("Invalid arch_name");
    }

    return arch_name_str;
}

static inline Arch get_arch_name(const std::string &arch_str){
    Arch arch;

    if ((arch_str == "jawbridge") || (arch_str == "JAWBRIDGE")) {
        arch = Arch::JAWBRIDGE;
    } else if ((arch_str == "grayskull") || (arch_str == "GRAYSKULL")) {
        arch = Arch::GRAYSKULL;
    } else if ((arch_str == "wormhole") || (arch_str == "WORMHOLE")){
        arch = Arch::WORMHOLE;
    } else if ((arch_str == "wormhole_b0") || (arch_str == "WORMHOLE_B0")){
        arch = Arch::WORMHOLE_B0;
    } else if ((arch_str == "blackhole") || (arch_str == "BLACKHOLE")){
        arch = Arch::BLACKHOLE;
    }else {
        throw std::runtime_error(
            "At LoadSocDescriptorFromYaml: \"" + arch_str + "\" is not recognized as Arch.");
    }

    return arch;
}

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
  xy_pair coord = xy_pair(0, 0);
  CoreType type;

  std::size_t l1_size = 0;
};

class SocDescriptor {
   public:
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

    int get_num_dram_channels() const;
    bool is_worker_core(const xy_pair &core) const;
    xy_pair get_core_for_dram_channel(int dram_chan, int subchannel) const;
    bool is_ethernet_core(const xy_pair &core) const;

    // Default constructor. Creates uninitialized object with public access to all of its attributes.
    // SocDescriptor() = default;
    // Constructor used to build object from device descriptor file.
    SocDescriptor(std::string device_descriptor_path);
    // Copy constructor
    SocDescriptor(const SocDescriptor &other) :
        arch(other.arch),
        grid_size(other.grid_size),
        physical_grid_size(other.physical_grid_size),
        worker_grid_size(other.worker_grid_size),
        cores(other.cores),
        arc_cores(other.arc_cores),
        workers(other.workers),
        harvested_workers(other.harvested_workers),
        pcie_cores(other.pcie_cores),
        worker_log_to_routing_x(other.worker_log_to_routing_x),
        worker_log_to_routing_y(other.worker_log_to_routing_y),
        routing_x_to_worker_x(other.routing_x_to_worker_x),
        routing_y_to_worker_y(other.routing_y_to_worker_y),
        dram_cores(other.dram_cores),
        dram_core_channel_map(other.dram_core_channel_map),
        ethernet_cores(other.ethernet_cores),
        ethernet_core_channel_map(other.ethernet_core_channel_map),
        trisc_sizes(other.trisc_sizes),
        device_descriptor_file_path(other.device_descriptor_file_path),
        overlay_version(other.overlay_version),
        unpacker_version(other.unpacker_version),
        dst_size_alignment(other.dst_size_alignment),
        packer_version(other.packer_version),
        worker_l1_size(other.worker_l1_size),
        eth_l1_size(other.eth_l1_size),
        noc_translation_id_enabled(other.noc_translation_id_enabled),
        dram_bank_size(other.dram_bank_size) {}

   private:
    void load_core_descriptors_from_device_descriptor(YAML::Node &device_descriptor_yaml);
    void load_soc_features_from_device_descriptor(YAML::Node &device_descriptor_yaml);
};

}  // namespace tt::umd