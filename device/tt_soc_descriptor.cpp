// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_xy_pair.h"
#include "yaml-cpp/yaml.h"
#include "tt_soc_descriptor.h"

#include <assert.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "fmt/core.h"

// #include "l1_address_map.h"

std::string format_node(tt_xy_pair xy) { return fmt::format("{}-{}", xy.x, xy.y); }

tt_xy_pair format_node(std::string str) {
  int x_coord;
  int y_coord;
  std::regex expr("([0-9]+)[-,xX]([0-9]+)");
  std::smatch x_y_pair;

  if (std::regex_search(str, x_y_pair, expr)) {
    x_coord = std::stoi(x_y_pair[1]);
    y_coord = std::stoi(x_y_pair[2]);
  } else {
    throw std::runtime_error(fmt::format("Could not parse the core id: {}", str));
  }

  tt_xy_pair xy(x_coord, y_coord);

  return xy;
}
const char* ws = " \t\n\r\f\v";

// trim from end of string (right)
inline std::string& rtrim(std::string& s, const char* t = ws)
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from beginning of string (left)
inline std::string& ltrim(std::string& s, const char* t = ws)
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from both ends of string (right then left)
inline std::string& trim(std::string& s, const char* t = ws)
{
    return ltrim(rtrim(s, t), t);
}

void tt_SocDescriptor::load_soc_features_from_device_descriptor(YAML::Node &device_descriptor_yaml) {
    noc_translation_id_enabled = device_descriptor_yaml["features"]["noc"] && device_descriptor_yaml["features"]["noc"]["translation_id_enabled"] ? device_descriptor_yaml["features"]["noc"]["translation_id_enabled"].as<bool>() : false;
    dst_size_alignment = device_descriptor_yaml["features"]["math"]["dst_size_alignment"].as<int>();
    worker_l1_size = device_descriptor_yaml["worker_l1_size"].as<int>();
    eth_l1_size = device_descriptor_yaml["eth_l1_size"].as<int>();
    dram_bank_size = device_descriptor_yaml["dram_bank_size"].as<uint64_t>();
}

void tt_SocDescriptor::load_core_descriptors_from_device_descriptor(YAML::Node &device_descriptor_yaml) {
    auto worker_l1_size = device_descriptor_yaml["worker_l1_size"].as<int>();
    auto eth_l1_size = device_descriptor_yaml["eth_l1_size"].as<int>();

    auto arc_cores_yaml = device_descriptor_yaml["arc"].as<std::vector<std::string>>();
    for (const auto &core_string : arc_cores_yaml) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = format_node(core_string);
        core_descriptor.type = CoreType::ARC;
        cores.insert({core_descriptor.coord, core_descriptor});
        arc_cores.push_back(core_descriptor.coord);
    }
    auto pcie_cores_yaml = device_descriptor_yaml["pcie"].as<std::vector<std::string>>();
    for (const auto &core_string : pcie_cores_yaml) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = format_node(core_string);
        core_descriptor.type = CoreType::PCIE;
        cores.insert({core_descriptor.coord, core_descriptor});
        pcie_cores.push_back(core_descriptor.coord);
    }

    int current_dram_channel = 0;
    for (auto channel_it = device_descriptor_yaml["dram"].begin(); channel_it != device_descriptor_yaml["dram"].end(); ++channel_it) {
        dram_cores.push_back({});
        auto &soc_dram_cores = dram_cores.at(dram_cores.size() - 1);
        const auto &dram_cores = (*channel_it).as<std::vector<std::string>>();
        for (unsigned int i = 0; i < dram_cores.size(); i++) {
            const auto &dram_core = dram_cores.at(i);
            CoreDescriptor core_descriptor;
            core_descriptor.coord = format_node(dram_core);
            core_descriptor.type = CoreType::DRAM;
            cores.insert({core_descriptor.coord, core_descriptor});
            soc_dram_cores.push_back(core_descriptor.coord);
            dram_core_channel_map[core_descriptor.coord] = {current_dram_channel, i};
        }
        current_dram_channel++;
    }
    auto eth_cores = device_descriptor_yaml["eth"].as<std::vector<std::string>>();
    int current_ethernet_channel = 0;
    for (const auto &core_string : eth_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = format_node(core_string);
        core_descriptor.type = CoreType::ETH;
        core_descriptor.l1_size = eth_l1_size;
        cores.insert({core_descriptor.coord, core_descriptor});
        ethernet_cores.push_back(core_descriptor.coord);

        ethernet_core_channel_map[core_descriptor.coord] = current_ethernet_channel;
        current_ethernet_channel++;
    }
    std::vector<std::string> worker_cores = device_descriptor_yaml["functional_workers"].as<std::vector<std::string>>();
    std::set<int> worker_routing_coords_x;
    std::set<int> worker_routing_coords_y;
    std::unordered_map<int,int> routing_coord_worker_x;
    std::unordered_map<int,int> routing_coord_worker_y;
    for (const auto &core_string : worker_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = format_node(core_string);
        core_descriptor.type = CoreType::WORKER;
        core_descriptor.l1_size = worker_l1_size;
        cores.insert({core_descriptor.coord, core_descriptor});
        workers.push_back(core_descriptor.coord);
        worker_routing_coords_x.insert(core_descriptor.coord.x);
        worker_routing_coords_y.insert(core_descriptor.coord.y);
    }

    int func_x_start = 0;
    int func_y_start = 0;
    std::set<int>::iterator it;
    for (it=worker_routing_coords_x.begin(); it!=worker_routing_coords_x.end(); ++it) {
        worker_log_to_routing_x[func_x_start] = *it;
        routing_x_to_worker_x[*it] = func_x_start;
        func_x_start++;
    }
    for (it=worker_routing_coords_y.begin(); it!=worker_routing_coords_y.end(); ++it) {
        worker_log_to_routing_y[func_y_start] = *it;
        routing_y_to_worker_y[*it] = func_y_start;
        func_y_start++;
    }

    worker_grid_size = tt_xy_pair(func_x_start, func_y_start);

    auto harvested_cores = device_descriptor_yaml["harvested_workers"].as<std::vector<std::string>>();
    for (const auto &core_string : harvested_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = format_node(core_string);
        core_descriptor.type = CoreType::HARVESTED;
        cores.insert({core_descriptor.coord, core_descriptor});
    }
    auto router_only_cores = device_descriptor_yaml["router_only"].as<std::vector<std::string>>();
    for (const auto &core_string : router_only_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = format_node(core_string);
        core_descriptor.type = CoreType::ROUTER_ONLY;
        cores.insert({core_descriptor.coord, core_descriptor});
    }
}

void tt_SocDescriptor::perform_harvesting(std::size_t harvesting_mask) {
    std::set<size_t> physical_x_unharvested;
    std::set<size_t> physical_y_unharvested;
    for (auto core : workers) {
        physical_x_unharvested.insert(core.x);
        physical_y_unharvested.insert(core.y);
    }

    std::set<std::size_t> harvested_x;
    std::set<std::size_t> harvested_y;

    std::size_t logical_y = 0;
    std::size_t logical_x = 0;
    while (harvesting_mask > 0) {
        if (harvesting_mask & 1) {
            if (arch == tt::ARCH::WORMHOLE_B0) {
                harvested_y.insert(logical_y);
            } else if (arch == tt::ARCH::BLACKHOLE) {
                harvested_x.insert(logical_x);
            }
        }
        logical_y++;
        logical_x++;
        harvesting_mask >>= 1;
    }

    std::size_t num_harvested_y = harvested_y.size();
    std::size_t num_harvested_x = harvested_x.size();

    std::size_t grid_size_x = worker_grid_size.x;
    std::size_t grid_size_y = worker_grid_size.y;

    logical_x_to_physical_x.resize(grid_size_x - num_harvested_x);
    logical_y_to_physical_y.resize(grid_size_y - num_harvested_y);

    logical_x_to_virtual_x.resize(grid_size_x - num_harvested_x);
    logical_y_to_virtual_y.resize(grid_size_y - num_harvested_y);

    auto physical_y_it = physical_y_unharvested.begin();
    logical_y = 0;
    for (size_t y = 0; y < grid_size_y; y++) {
        if (harvested_y.find(y) == harvested_y.end()) {
            logical_y_to_physical_y[logical_y] = *physical_y_it;
            if (physical_y_to_logical_y.find(*physical_y_it) != physical_y_to_logical_y.end()) {
                throw std::runtime_error("Duplicate physical y coordinate found in the worker cores");
            }
            physical_y_to_logical_y[*physical_y_it] = logical_y;
            logical_y++;
            physical_y_it++;
        } else {
            physical_y_it++;
        }
    }

    auto physical_x_it = physical_x_unharvested.begin();
    logical_x = 0;
    for(std::size_t x = 0; x < grid_size_x; x++) {
        if (harvested_x.find(x) == harvested_x.end()) {
            logical_x_to_physical_x[logical_x] = *physical_x_it;
            if (physical_x_to_logical_x.find(*physical_x_it) != physical_x_to_logical_x.end()) {
                throw std::runtime_error("Duplicate physical x coordinate found in the worker cores");
            }
            physical_x_to_logical_x[*physical_x_it] = logical_x;
            logical_x++;
            physical_x_it++;
        } else {
            physical_x_it++;
        }
    }

    physical_y_it = physical_y_unharvested.begin();
    for (std::size_t y = 0; y < logical_y_to_virtual_y.size(); y++) {
        logical_y_to_virtual_y[y] = *physical_y_it;
        if (virtual_y_to_logical_y.find(*physical_y_it) != virtual_y_to_logical_y.end()) {
            throw std::runtime_error("Duplicate virtual y coordinate found in the worker cores");
        }
        virtual_y_to_logical_y[*physical_y_it] = y;
        physical_y_it++;
    }

    physical_x_it = physical_x_unharvested.begin();
    for (std::size_t x = 0; x < logical_x_to_virtual_x.size(); x++) {
        logical_x_to_virtual_x[x] = *physical_x_it;
        if (virtual_x_to_logical_x.find(*physical_x_it) != virtual_x_to_logical_x.end()) {
            throw std::runtime_error("Duplicate virtual x coordinate found in the worker cores");
        }
        virtual_x_to_logical_x[*physical_x_it] = x;
        physical_x_it++;
    }
}

tt_physical_coords tt_SocDescriptor::logical_to_physical_coords(tt_logical_coords logical_coords) {
    // log_assert(logical_coords.x < logical_x_to_physical_x.size());
    // log_assert(logical_coords.y < logical_y_to_physical_y.size());
    return tt_physical_coords(logical_x_to_physical_x[logical_coords.x], logical_y_to_physical_y[logical_coords.y]);
}

// TODO(pjanevski): is it enough just to add 18 to the logical coordinates
// in order to get the translated coordinates?
tt_translated_coords tt_SocDescriptor::logical_to_translated_coords(tt_logical_coords logical_coords) {
    static const std::size_t translated_offset = 18;
    return tt_translated_coords(logical_coords.x + translated_offset, logical_coords.y + translated_offset);
}

tt_logical_coords tt_SocDescriptor::physical_to_logical_coords(tt_physical_coords physical_coords) {
    return tt_logical_coords(physical_x_to_logical_x[physical_coords.x], physical_y_to_logical_y[physical_coords.y]);
}

tt_translated_coords tt_SocDescriptor::physical_to_translated_coords(tt_physical_coords physical_coords) {
    return tt_translated_coords(0, 0);
}

tt_virtual_coords tt_SocDescriptor::logical_to_virtual_coords(tt_logical_coords logical_coords) {
    return tt_virtual_coords(logical_x_to_virtual_x[logical_coords.x], logical_y_to_virtual_y[logical_coords.y]);
}

tt_logical_coords tt_SocDescriptor::virtual_to_logical_coords(tt_virtual_coords virtual_coords) {
    return tt_logical_coords(virtual_x_to_logical_x[virtual_coords.x], virtual_y_to_logical_y[virtual_coords.y]);
}

tt_SocDescriptor::tt_SocDescriptor(std::string device_descriptor_path, std::size_t harvesting_mask) {
    std::ifstream fdesc(device_descriptor_path);
    if (fdesc.fail()) {
        throw std::runtime_error(fmt::format("Error: device descriptor file {} does not exist!", device_descriptor_path));
    }
    fdesc.close();

    YAML::Node device_descriptor_yaml = YAML::LoadFile(device_descriptor_path);

    auto grid_size_x = device_descriptor_yaml["grid"]["x_size"].as<int>();
    auto grid_size_y = device_descriptor_yaml["grid"]["y_size"].as<int>();
    int physical_grid_size_x = device_descriptor_yaml["physical"] && device_descriptor_yaml["physical"]["x_size"] ?
                                device_descriptor_yaml["physical"]["x_size"].as<int>() : grid_size_x;
    int physical_grid_size_y = device_descriptor_yaml["physical"] && device_descriptor_yaml["physical"]["y_size"] ?
                                device_descriptor_yaml["physical"]["y_size"].as<int>() : grid_size_y;
    load_core_descriptors_from_device_descriptor(device_descriptor_yaml);
    grid_size = tt_xy_pair(grid_size_x, grid_size_y);
    physical_grid_size = tt_xy_pair(physical_grid_size_x, physical_grid_size_y);
    device_descriptor_file_path = device_descriptor_path;
    std::string arch_name_value = device_descriptor_yaml["arch_name"].as<std::string>();
    arch_name_value = trim(arch_name_value);
    arch = get_arch_name(arch_name_value);
    load_soc_features_from_device_descriptor(device_descriptor_yaml);
    perform_harvesting(harvesting_mask);
}

int tt_SocDescriptor::get_num_dram_channels() const {
    int num_channels = 0;
    for (auto& dram_core : dram_cores) {
        if (dram_core.size() > 0) {
            num_channels++;
        }
    }
    return num_channels;
}

bool tt_SocDescriptor::is_worker_core(const tt_xy_pair &core) const {
    return (
        routing_x_to_worker_x.find(core.x) != routing_x_to_worker_x.end() &&
        routing_y_to_worker_y.find(core.y) != routing_y_to_worker_y.end());
}

tt_xy_pair tt_SocDescriptor::get_core_for_dram_channel(int dram_chan, int subchannel) const {
    return this->dram_cores.at(dram_chan).at(subchannel);
};

bool tt_SocDescriptor::is_ethernet_core(const tt_xy_pair &core) const {
    return this->ethernet_core_channel_map.find(core) != ethernet_core_channel_map.end();
}

std::ostream &operator<<(std::ostream &out, const tt::ARCH &arch_name) {
    if (arch_name == tt::ARCH::JAWBRIDGE) {
        out << "jawbridge";
    } else if (arch_name == tt::ARCH::Invalid) {
        out << "none";
    } else if (arch_name == tt::ARCH::GRAYSKULL) {
        out << "grayskull";
    } else if (arch_name == tt::ARCH::WORMHOLE) {
        out << "wormhole";
    } else if (arch_name == tt::ARCH::WORMHOLE_B0) {
        out << "wormhole_b0";
    } else if (arch_name == tt::ARCH::BLACKHOLE) {
        out << "blackhole"; //Just how many ARCH-to-string functions do we plan to have, anyway?
    } else {
        out << "ArchNameSerializationNotImplemented";
    }

    return out;
}
