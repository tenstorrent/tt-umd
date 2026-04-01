// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/soc_arch_descriptor.hpp"

#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt::umd {

SocArchDescriptor SocArchDescriptor::create(tt::ARCH arch_enum) {
    SocArchDescriptor desc;

    switch (arch_enum) {
        case tt::ARCH::WORMHOLE_B0:
            desc.arch = tt::ARCH::WORMHOLE_B0;
            desc.grid_size = wormhole::GRID_SIZE;
            desc.tensix_cores = wormhole::TENSIX_CORES_NOC0;
            desc.dram_cores = wormhole::DRAM_CORES_NOC0;
            desc.eth_cores = wormhole::ETH_CORES_NOC0;
            desc.arc_cores = wormhole::ARC_CORES_NOC0;
            desc.pcie_cores = wormhole::PCIE_CORES_NOC0;
            desc.router_cores = wormhole::ROUTER_CORES_NOC0;
            desc.security_cores = wormhole::SECURITY_CORES_NOC0;
            desc.l2cpu_cores = wormhole::L2CPU_CORES_NOC0;
            desc.worker_l1_size = wormhole::TENSIX_L1_SIZE;
            desc.eth_l1_size = wormhole::ETH_L1_SIZE;
            desc.dram_bank_size = wormhole::DRAM_BANK_SIZE;
            desc.noc0_x_to_noc1_x = wormhole::NOC0_X_TO_NOC1_X;
            desc.noc0_y_to_noc1_y = wormhole::NOC0_Y_TO_NOC1_Y;
            break;
        case tt::ARCH::BLACKHOLE:
            desc.arch = tt::ARCH::BLACKHOLE;
            desc.grid_size = blackhole::GRID_SIZE;
            desc.tensix_cores = blackhole::TENSIX_CORES_NOC0;
            desc.dram_cores = blackhole::DRAM_CORES_NOC0;
            desc.eth_cores = blackhole::ETH_CORES_NOC0;
            desc.arc_cores = blackhole::ARC_CORES_NOC0;
            desc.pcie_cores = blackhole::PCIE_CORES_NOC0;
            desc.router_cores = blackhole::ROUTER_CORES_NOC0;
            desc.security_cores = blackhole::SECURITY_CORES_NOC0;
            desc.l2cpu_cores = blackhole::L2CPU_CORES_NOC0;
            desc.worker_l1_size = blackhole::TENSIX_L1_SIZE;
            desc.eth_l1_size = blackhole::ETH_L1_SIZE;
            desc.dram_bank_size = blackhole::DRAM_BANK_SIZE;
            desc.noc0_x_to_noc1_x = blackhole::NOC0_X_TO_NOC1_X;
            desc.noc0_y_to_noc1_y = blackhole::NOC0_Y_TO_NOC1_Y;
            break;
        case tt::ARCH::QUASAR:
            desc.arch = tt::ARCH::QUASAR;
            desc.grid_size = grendel::GRID_SIZE;
            desc.tensix_cores = grendel::TENSIX_CORES_NOC0;
            desc.dram_cores = grendel::DRAM_CORES_NOC0;
            desc.eth_cores = grendel::ETH_CORES_NOC0;
            desc.arc_cores = grendel::ARC_CORES_NOC0;
            desc.pcie_cores = grendel::PCIE_CORES_NOC0;
            desc.router_cores = grendel::ROUTER_CORES_NOC0;
            desc.security_cores = grendel::SECURITY_CORES_NOC0;
            desc.l2cpu_cores = grendel::L2CPU_CORES_NOC0;
            desc.dispatch_cores = grendel::DISPATCH_CORES_NOC0;
            desc.worker_l1_size = grendel::TENSIX_L1_SIZE;
            desc.eth_l1_size = grendel::ETH_L1_SIZE;
            desc.dram_bank_size = grendel::DRAM_BANK_SIZE;
            desc.noc0_x_to_noc1_x = grendel::NOC0_X_TO_NOC1_X;
            desc.noc0_y_to_noc1_y = grendel::NOC0_Y_TO_NOC1_Y;
            break;
        default:
            throw std::runtime_error("Invalid architecture for creating SocArchDescriptor.");
    }

    desc.build_derived_data();
    return desc;
}

SocArchDescriptor SocArchDescriptor::create(const std::string& device_descriptor_path) {
    std::ifstream fdesc(device_descriptor_path);
    if (fdesc.fail()) {
        throw std::runtime_error(
            fmt::format("Error: device descriptor file {} does not exist!", device_descriptor_path));
    }
    fdesc.close();

    YAML::Node device_descriptor_yaml = YAML::LoadFile(device_descriptor_path);

    SocArchDescriptor desc;
    desc.device_descriptor_file_path = device_descriptor_path;
    desc.load_from_yaml(device_descriptor_yaml);
    desc.build_derived_data();
    return desc;
}

tt::ARCH SocArchDescriptor::get_arch_from_path(const std::string& soc_descriptor_path) {
    YAML::Node device_descriptor_yaml = YAML::LoadFile(soc_descriptor_path);
    return tt::arch_from_str(device_descriptor_yaml["arch_name"].as<std::string>());
}

tt_xy_pair SocArchDescriptor::calculate_grid_size(const std::vector<tt_xy_pair>& cores) {
    std::unordered_set<size_t> x;
    std::unordered_set<size_t> y;
    for (auto core : cores) {
        x.insert(core.x);
        y.insert(core.y);
    }
    return {x.size(), y.size()};
}

tt_xy_pair SocArchDescriptor::get_grid_size_from_path(const std::string& soc_descriptor_path) {
    YAML::Node device_descriptor_yaml = YAML::LoadFile(soc_descriptor_path);
    return tt_xy_pair(
        device_descriptor_yaml["grid"]["x_size"].as<int>(), device_descriptor_yaml["grid"]["y_size"].as<int>());
}

void SocArchDescriptor::build_derived_data() {
    // Build core descriptor map.
    for (const auto& arc_core : arc_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = arc_core;
        core_descriptor.type = CoreType::ARC;
        cores.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& pcie_core : pcie_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = pcie_core;
        core_descriptor.type = CoreType::PCIE;
        cores.insert({core_descriptor.coord, core_descriptor});
    }

    int current_dram_channel = 0;
    for (const auto& channel : dram_cores) {
        for (unsigned int i = 0; i < channel.size(); i++) {
            CoreDescriptor core_descriptor;
            core_descriptor.coord = channel[i];
            core_descriptor.type = CoreType::DRAM;
            cores.insert({core_descriptor.coord, core_descriptor});
            dram_core_channel_map[core_descriptor.coord] = {current_dram_channel, static_cast<int>(i)};
        }
        current_dram_channel++;
    }

    int current_ethernet_channel = 0;
    for (const auto& eth_core : eth_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = eth_core;
        core_descriptor.type = CoreType::ETH;
        core_descriptor.l1_size = eth_l1_size;
        cores.insert({core_descriptor.coord, core_descriptor});
        ethernet_core_channel_map[core_descriptor.coord] = current_ethernet_channel;
        current_ethernet_channel++;
    }

    std::set<int> worker_routing_coords_x;
    std::set<int> worker_routing_coords_y;
    for (const auto& tensix_core : tensix_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = tensix_core;
        core_descriptor.type = CoreType::WORKER;
        core_descriptor.l1_size = worker_l1_size;
        cores.insert({core_descriptor.coord, core_descriptor});
        worker_routing_coords_x.insert(core_descriptor.coord.x);
        worker_routing_coords_y.insert(core_descriptor.coord.y);
    }

    worker_grid_size = tt_xy_pair(worker_routing_coords_x.size(), worker_routing_coords_y.size());

    for (const auto& router_core : router_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = router_core;
        core_descriptor.type = CoreType::ROUTER_ONLY;
        cores.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& security_core : security_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = security_core;
        core_descriptor.type = CoreType::SECURITY;
        cores.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& l2cpu_core : l2cpu_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = l2cpu_core;
        core_descriptor.type = CoreType::L2CPU;
        cores.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& dispatch_core : dispatch_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = dispatch_core;
        core_descriptor.type = CoreType::DISPATCH;
        cores.insert({core_descriptor.coord, core_descriptor});
    }
}

// Trimming helpers for YAML parsing.
static const char* ws = " \t\n\r\f\v";

static inline std::string& rtrim(std::string& s, const char* t = ws) {
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

static inline std::string& ltrim(std::string& s, const char* t = ws) {
    s.erase(0, s.find_first_not_of(t));
    return s;
}

static inline std::string& trim(std::string& s, const char* t = ws) { return ltrim(rtrim(s, t), t); }

void SocArchDescriptor::load_from_yaml(YAML::Node& device_descriptor_yaml) {
    grid_size = tt_xy_pair(
        device_descriptor_yaml["grid"]["x_size"].as<int>(), device_descriptor_yaml["grid"]["y_size"].as<int>());

    std::string arch_name_value = device_descriptor_yaml["arch_name"].as<std::string>();
    arch_name_value = trim(arch_name_value);
    arch = tt::arch_from_str(arch_name_value);

    tensix_cores = SocArchDescriptor::convert_to_tt_xy_pair(
        device_descriptor_yaml["functional_workers"].as<std::vector<std::string>>());
    dram_cores = SocArchDescriptor::convert_dram_cores_from_yaml(device_descriptor_yaml, "dram");
    pcie_cores =
        SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["pcie"].as<std::vector<std::string>>());
    eth_cores = SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["eth"].as<std::vector<std::string>>());
    arc_cores = SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["arc"].as<std::vector<std::string>>());
    router_cores =
        SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["router_only"].as<std::vector<std::string>>());

    if (device_descriptor_yaml["l2cpu"].IsDefined()) {
        l2cpu_cores =
            SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["l2cpu"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["security"].IsDefined()) {
        security_cores =
            SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["security"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["dispatch"].IsDefined()) {
        dispatch_cores =
            SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["dispatch"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["noc0_x_to_noc1_x"].IsDefined()) {
        noc0_x_to_noc1_x = device_descriptor_yaml["noc0_x_to_noc1_x"].as<std::vector<uint32_t>>();
        noc0_y_to_noc1_y = device_descriptor_yaml["noc0_y_to_noc1_y"].as<std::vector<uint32_t>>();
    }

    worker_l1_size = device_descriptor_yaml["worker_l1_size"].as<uint32_t>();
    eth_l1_size = device_descriptor_yaml["eth_l1_size"].as<uint32_t>();
    dram_bank_size = device_descriptor_yaml["dram_bank_size"].as<uint64_t>();
}

std::vector<tt_xy_pair> SocArchDescriptor::convert_to_tt_xy_pair(const std::vector<std::string>& core_strings) {
    std::vector<tt_xy_pair> core_pairs;
    core_pairs.reserve(core_strings.size());
    for (const auto& core_string : core_strings) {
        core_pairs.push_back(format_node(core_string));
    }
    return core_pairs;
}

std::vector<std::vector<tt_xy_pair>> SocArchDescriptor::convert_dram_cores_from_yaml(
    YAML::Node& device_descriptor_yaml, const std::string& dram_core) {
    std::vector<std::vector<tt_xy_pair>> result;
    for (auto channel_it = device_descriptor_yaml[dram_core].begin();
         channel_it != device_descriptor_yaml[dram_core].end();
         ++channel_it) {
        result.push_back(convert_to_tt_xy_pair((*channel_it).as<std::vector<std::string>>()));
    }
    return result;
}

}  // namespace tt::umd
