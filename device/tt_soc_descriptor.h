/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>

#include <iostream>
#include <string>
#include <cstdint>

#include "tt_xy_pair.h"
#include "device/tt_arch_types.h"

namespace YAML {
    class Node;
}

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
    } else if (arch_name == tt::ARCH::BLACKHOLE) {
        arch_name_str = "blackhole";
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
    } else if ((arch_str == "blackhole") || (arch_str == "BLACKHOLE")){
        arch = tt::ARCH::BLACKHOLE;
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
};

//! tt_SocDescriptor contains information regarding the SOC configuration targetted.
/*!
    Should only contain relevant configuration for SOC
*/
class tt_SocDescriptor {
    public:
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
    uint64_t dram_bank_size;

    int get_num_dram_channels() const;
    bool is_worker_core(const tt_xy_pair &core) const;
    tt_xy_pair get_core_for_dram_channel(int dram_chan, int subchannel) const;
    bool is_ethernet_core(const tt_xy_pair& core) const;

    // Default constructor. Creates uninitialized object with public access to all of its attributes.
    tt_SocDescriptor() = default;
    // Constructor used to build object from device descriptor file.
    tt_SocDescriptor(std::string device_descriptor_path);
    // Copy constructor
    tt_SocDescriptor(const tt_SocDescriptor& other) :
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
        dram_bank_size(other.dram_bank_size) {
    }
    private:
    void load_core_descriptors_from_device_descriptor(YAML::Node &device_descriptor_yaml);
    void load_soc_features_from_device_descriptor(YAML::Node &device_descriptor_yaml);
};

// Allocates a new soc descriptor on the heap. Returns an owning pointer.
// std::unique_ptr<tt_SocDescriptor> load_soc_descriptor_from_yaml(std::string device_descriptor_file_path);
