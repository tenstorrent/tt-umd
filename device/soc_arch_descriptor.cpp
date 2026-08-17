// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/soc_arch_descriptor.hpp"

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SocArchDescriptor::SocArchDescriptor(tt::ARCH arch_enum) {
    arch_ = arch_enum;
    // Explicit non-virtual call: dispatch during construction always resolves to this class, and
    // qualifying makes that intent clear (see the init() declaration).
    SocArchDescriptor::init();
}

SocArchDescriptor::SocArchDescriptor(const std::string& soc_descriptor_path) {
    device_descriptor_file_path_ = soc_descriptor_path;
    SocArchDescriptor::init();
}

void SocArchDescriptor::init() {
    // When constructed from a YAML descriptor the file path is set; otherwise populate from the
    // hardcoded architecture constants selected by arch_.
    if (!device_descriptor_file_path_.empty()) {
        std::ifstream soc_descriptor_file(device_descriptor_file_path_);
        if (soc_descriptor_file.fail()) {
            UMD_THROW(
                error::RuntimeError, "SoC descriptor file does not exist at path: " + device_descriptor_file_path_);
        }
        soc_descriptor_file.close();
        YAML::Node device_descriptor_yaml = YAML::LoadFile(device_descriptor_file_path_);
        load_from_yaml(device_descriptor_yaml);
        build_derived_data();
        return;
    }

    switch (arch_) {
        case tt::ARCH::WORMHOLE_B0:
            grid_size_ = wormhole::GRID_SIZE;
            tensix_cores_ = wormhole::TENSIX_CORES_NOC0;
            dram_cores_ = wormhole::DRAM_CORES_NOC0;
            eth_cores_ = wormhole::ETH_CORES_NOC0;
            firmware_cores_ = wormhole::ARC_CORES_NOC0;
            pcie_cores_ = wormhole::PCIE_CORES_NOC0;
            router_cores_ = wormhole::ROUTER_CORES_NOC0;
            security_cores_ = wormhole::SECURITY_CORES_NOC0;
            l2cpu_cores_ = wormhole::L2CPU_CORES_NOC0;
            worker_l1_size_ = wormhole::TENSIX_L1_SIZE;
            eth_l1_size_ = wormhole::ETH_L1_SIZE;
            dram_bank_size_ = wormhole::DRAM_BANK_SIZE;
            noc0_x_to_noc1_x_ = wormhole::NOC0_X_TO_NOC1_X;
            noc0_y_to_noc1_y_ = wormhole::NOC0_Y_TO_NOC1_Y;
            break;
        case tt::ARCH::BLACKHOLE:
            grid_size_ = blackhole::GRID_SIZE;
            tensix_cores_ = blackhole::TENSIX_CORES_NOC0;
            dram_cores_ = blackhole::DRAM_CORES_NOC0;
            eth_cores_ = blackhole::ETH_CORES_NOC0;
            firmware_cores_ = blackhole::ARC_CORES_NOC0;
            pcie_cores_ = blackhole::PCIE_CORES_NOC0;
            router_cores_ = blackhole::ROUTER_CORES_NOC0;
            security_cores_ = blackhole::SECURITY_CORES_NOC0;
            l2cpu_cores_ = blackhole::L2CPU_CORES_NOC0;
            worker_l1_size_ = blackhole::TENSIX_L1_SIZE;
            eth_l1_size_ = blackhole::ETH_L1_SIZE;
            dram_bank_size_ = blackhole::DRAM_BANK_SIZE;
            noc0_x_to_noc1_x_ = blackhole::NOC0_X_TO_NOC1_X;
            noc0_y_to_noc1_y_ = blackhole::NOC0_Y_TO_NOC1_Y;
            break;
        case tt::ARCH::QUASAR:
            grid_size_ = grendel::GRID_SIZE;
            tensix_cores_ = grendel::TENSIX_CORES_NOC0;
            dram_cores_ = grendel::DRAM_CORES_NOC0;
            eth_cores_ = grendel::ETH_CORES_NOC0;
            firmware_cores_ = grendel::ARC_CORES_NOC0;
            pcie_cores_ = grendel::PCIE_CORES_NOC0;
            router_cores_ = grendel::ROUTER_CORES_NOC0;
            security_cores_ = grendel::SECURITY_CORES_NOC0;
            l2cpu_cores_ = grendel::L2CPU_CORES_NOC0;
            dispatch_cores_ = grendel::DISPATCH_CORES_NOC0;
            worker_l1_size_ = grendel::TENSIX_L1_SIZE;
            eth_l1_size_ = grendel::ETH_L1_SIZE;
            dram_bank_size_ = grendel::DRAM_BANK_SIZE;
            noc0_x_to_noc1_x_ = grendel::NOC0_X_TO_NOC1_X;
            noc0_y_to_noc1_y_ = grendel::NOC0_Y_TO_NOC1_Y;
            break;
        default:
            UMD_THROW(error::RuntimeError, "Invalid architecture for creating SocArchDescriptor.");
    }

    build_derived_data();
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
    // Clear derived data in case this is called more than once.
    cores_.clear();
    dram_core_channel_map_.clear();
    ethernet_core_channel_map_.clear();

    // Build core descriptor map.
    for (const auto& firmware_core : firmware_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = firmware_core;
        core_descriptor.type = CoreType::ARC;
        cores_.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& pcie_core : pcie_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = pcie_core;
        core_descriptor.type = CoreType::PCIE;
        cores_.insert({core_descriptor.coord, core_descriptor});
    }

    int current_dram_channel = 0;
    for (const auto& channel : dram_cores_) {
        for (unsigned int i = 0; i < channel.size(); i++) {
            CoreDescriptor core_descriptor;
            core_descriptor.coord = channel[i];
            core_descriptor.type = CoreType::DRAM;
            cores_.insert({core_descriptor.coord, core_descriptor});
            dram_core_channel_map_[core_descriptor.coord] = {current_dram_channel, static_cast<int>(i)};
        }
        current_dram_channel++;
    }

    int current_ethernet_channel = 0;
    for (const auto& eth_core : eth_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = eth_core;
        core_descriptor.type = CoreType::ETH;
        core_descriptor.l1_size = eth_l1_size_;
        cores_.insert({core_descriptor.coord, core_descriptor});
        ethernet_core_channel_map_[core_descriptor.coord] = current_ethernet_channel;
        current_ethernet_channel++;
    }

    std::set<int> worker_routing_coords_x;
    std::set<int> worker_routing_coords_y;
    for (const auto& tensix_core : tensix_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = tensix_core;
        core_descriptor.type = CoreType::WORKER;
        core_descriptor.l1_size = worker_l1_size_;
        cores_.insert({core_descriptor.coord, core_descriptor});
        worker_routing_coords_x.insert(core_descriptor.coord.x);
        worker_routing_coords_y.insert(core_descriptor.coord.y);
    }

    worker_grid_size_ = tt_xy_pair(worker_routing_coords_x.size(), worker_routing_coords_y.size());

    for (const auto& router_core : router_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = router_core;
        core_descriptor.type = CoreType::ROUTER_ONLY;
        cores_.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& security_core : security_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = security_core;
        core_descriptor.type = CoreType::SECURITY;
        cores_.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& l2cpu_core : l2cpu_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = l2cpu_core;
        core_descriptor.type = CoreType::L2CPU;
        cores_.insert({core_descriptor.coord, core_descriptor});
    }

    for (const auto& dispatch_core : dispatch_cores_) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = dispatch_core;
        core_descriptor.type = CoreType::DISPATCH;
        cores_.insert({core_descriptor.coord, core_descriptor});
    }
}

tt_xy_pair format_node(const std::string& str) {
    // Find the separator character.
    size_t sep_pos = std::string::npos;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '-') {
            sep_pos = i;
            break;
        }
    }

    if (sep_pos == std::string::npos || sep_pos == 0 || sep_pos >= str.size() - 1) {
        UMD_THROW(error::RuntimeError, fmt::format("Could not parse core coordinate: {}", str));
    }

    try {
        const char* str_cstr = str.c_str();
        int x_coord = std::atoi(str_cstr);
        int y_coord = std::atoi(str_cstr + sep_pos + 1);
        return tt_xy_pair(x_coord, y_coord);
    } catch (...) {
        UMD_THROW(error::RuntimeError, fmt::format("Could not parse core coordinate:  {}", str));
    }
}

// Trimming helpers for YAML parsing.
static const char* ws = " \t\n\r\f\v";

static inline std::string& rtrim(std::string& s, const char* t = ws) {
    auto pos = s.find_last_not_of(t);
    if (pos == std::string::npos) {
        s.clear();
    } else {
        s.erase(pos + 1);
    }
    return s;
}

static inline std::string& ltrim(std::string& s, const char* t = ws) {
    s.erase(0, s.find_first_not_of(t));
    return s;
}

static inline std::string& trim(std::string& s, const char* t = ws) { return ltrim(rtrim(s, t), t); }

void SocArchDescriptor::load_from_yaml(YAML::Node& device_descriptor_yaml) {
    grid_size_ = tt_xy_pair(
        device_descriptor_yaml["grid"]["x_size"].as<int>(), device_descriptor_yaml["grid"]["y_size"].as<int>());

    std::string arch_name_value = device_descriptor_yaml["arch_name"].as<std::string>();
    arch_name_value = trim(arch_name_value);
    arch_ = tt::arch_from_str(arch_name_value);

    tensix_cores_ = SocArchDescriptor::convert_to_tt_xy_pair(
        device_descriptor_yaml["functional_workers"].as<std::vector<std::string>>());
    dram_cores_ = SocArchDescriptor::convert_dram_cores_from_yaml(device_descriptor_yaml, "dram");
    pcie_cores_ =
        SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["pcie"].as<std::vector<std::string>>());
    eth_cores_ = SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["eth"].as<std::vector<std::string>>());
    firmware_cores_ =
        SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["arc"].as<std::vector<std::string>>());
    router_cores_ =
        SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["router_only"].as<std::vector<std::string>>());

    if (device_descriptor_yaml["l2cpu"].IsDefined()) {
        l2cpu_cores_ =
            SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["l2cpu"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["security"].IsDefined()) {
        security_cores_ =
            SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["security"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["dispatch"].IsDefined()) {
        dispatch_cores_ =
            SocArchDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["dispatch"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["noc0_x_to_noc1_x"].IsDefined()) {
        noc0_x_to_noc1_x_ = device_descriptor_yaml["noc0_x_to_noc1_x"].as<std::vector<uint32_t>>();
        noc0_y_to_noc1_y_ = device_descriptor_yaml["noc0_y_to_noc1_y"].as<std::vector<uint32_t>>();
    }

    worker_l1_size_ = device_descriptor_yaml["worker_l1_size"].as<uint32_t>();
    eth_l1_size_ = device_descriptor_yaml["eth_l1_size"].as<uint32_t>();
    dram_bank_size_ = device_descriptor_yaml["dram_bank_size"].as<uint64_t>();

    if (device_descriptor_yaml["features"].IsDefined()) {
        auto features = device_descriptor_yaml["features"];
        if (features["overlay"].IsDefined() && features["overlay"]["version"].IsDefined()) {
            overlay_version_ = features["overlay"]["version"].as<int>();
        }
        if (features["unpacker"].IsDefined() && features["unpacker"]["version"].IsDefined()) {
            unpacker_version_ = features["unpacker"]["version"].as<int>();
        }
        if (features["math"].IsDefined() && features["math"]["dst_size_alignment"].IsDefined()) {
            dst_size_alignment_ = features["math"]["dst_size_alignment"].as<int>();
        }
        if (features["packer"].IsDefined() && features["packer"]["version"].IsDefined()) {
            packer_version_ = features["packer"]["version"].as<int>();
        }
    }
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
