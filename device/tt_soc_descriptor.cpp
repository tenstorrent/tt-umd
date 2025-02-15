// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_soc_descriptor.h"

#include <assert.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_set>

#include "api/umd/device/blackhole_implementation.h"
#include "api/umd/device/tt_soc_descriptor.h"
#include "fmt/core.h"
#include "logger.hpp"
#include "utils.hpp"
#include "yaml-cpp/yaml.h"

// #include "l1_address_map.h"

using namespace tt::umd;

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

const char *ws = " \t\n\r\f\v";

// trim from end of string (right)
inline std::string &rtrim(std::string &s, const char *t = ws) {
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from beginning of string (left)
inline std::string &ltrim(std::string &s, const char *t = ws) {
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from both ends of string (right then left)
inline std::string &trim(std::string &s, const char *t = ws) { return ltrim(rtrim(s, t), t); }

void tt_SocDescriptor::load_soc_features_from_device_descriptor(YAML::Node &device_descriptor_yaml) {
    overlay_version = device_descriptor_yaml["features"]["overlay"]["version"].as<int>();
    // TODO: Check whether this is a valid value, and whether it is used.
    // Also check if this is the same thing as noc_translation in cluster descriptor.
    noc_translation_id_enabled =
        device_descriptor_yaml["features"]["noc"] && device_descriptor_yaml["features"]["noc"]["translation_id_enabled"]
            ? device_descriptor_yaml["features"]["noc"]["translation_id_enabled"].as<bool>()
            : false;
    packer_version = device_descriptor_yaml["features"]["packer"]["version"].as<int>();
    unpacker_version = device_descriptor_yaml["features"]["unpacker"]["version"].as<int>();
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
    for (auto channel_it = device_descriptor_yaml["dram"].begin(); channel_it != device_descriptor_yaml["dram"].end();
         ++channel_it) {
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
    std::unordered_map<int, int> routing_coord_worker_x;
    std::unordered_map<int, int> routing_coord_worker_y;
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
    for (it = worker_routing_coords_x.begin(); it != worker_routing_coords_x.end(); ++it) {
        worker_log_to_routing_x[func_x_start] = *it;
        routing_x_to_worker_x[*it] = func_x_start;
        func_x_start++;
    }
    for (it = worker_routing_coords_y.begin(); it != worker_routing_coords_y.end(); ++it) {
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
        router_cores.push_back(core_descriptor.coord);
    }
}

tt_xy_pair tt_SocDescriptor::calculate_grid_size(const std::vector<tt_xy_pair> &cores) {
    std::unordered_set<size_t> x;
    std::unordered_set<size_t> y;
    for (auto core : cores) {
        x.insert(core.x);
        y.insert(core.y);
    }
    return {x.size(), y.size()};
}

void tt_SocDescriptor::create_coordinate_manager(
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
    const BoardType board_type,
    const bool is_chip_remote) {
    const tt_xy_pair dram_grid_size = tt_xy_pair(dram_cores.size(), dram_cores.empty() ? 0 : dram_cores[0].size());
    const tt_xy_pair arc_grid_size = tt_SocDescriptor::calculate_grid_size(arc_cores);
    tt_xy_pair pcie_grid_size = tt_SocDescriptor::calculate_grid_size(pcie_cores);

    std::vector<tt_xy_pair> dram_cores_unpacked;
    for (const auto &vec : dram_cores) {
        for (const auto &core : vec) {
            dram_cores_unpacked.push_back(core);
        }
    }

    // We have a specific case where we have a fixed soc, but differently wired based on the board type, effectively
    // enabling only one of the two pci cores. This is currently a unique case, and if another similar case shows up, we
    // can figure out a better abstraction.
    if (arch == tt::ARCH::BLACKHOLE && board_type != BoardType::UNKNOWN) {
        auto pcie_cores_for_type = blackhole::get_pcie_cores(board_type, is_chip_remote);
        // Verify that the required pcie core was already mentioned in the device descriptor.
        for (const auto &core : pcie_cores_for_type) {
            if (std::find(pcie_cores.begin(), pcie_cores.end(), core) == pcie_cores.end()) {
                throw std::runtime_error(
                    fmt::format("Error: Required pcie core {} not found in the device descriptor!", format_node(core)));
            }
        }
        // Add the unused pcie cores as router cores.
        for (const auto &core : pcie_cores) {
            if (std::find(pcie_cores_for_type.begin(), pcie_cores_for_type.end(), core) == pcie_cores_for_type.end()) {
                router_cores.push_back(core);
            }
        }

        pcie_cores = pcie_cores_for_type;
        pcie_grid_size = tt_SocDescriptor::calculate_grid_size(pcie_cores);
    }

    coordinate_manager = CoordinateManager::create_coordinate_manager(
        arch,
        noc_translation_enabled,
        harvesting_masks,
        worker_grid_size,
        workers,
        dram_grid_size,
        dram_cores_unpacked,
        ethernet_cores,
        arc_grid_size,
        arc_cores,
        pcie_grid_size,
        pcie_cores,
        router_cores);
    get_cores_and_grid_size_from_coordinate_manager();
}

tt::umd::CoreCoord tt_SocDescriptor::translate_coord_to(
    const tt::umd::CoreCoord core_coord, const CoordSystem coord_system) const {
    return coordinate_manager->translate_coord_to(core_coord, coord_system);
}

tt::umd::CoreCoord tt_SocDescriptor::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    return coordinate_manager->get_coord_at(core, coord_system);
}

tt::umd::CoreCoord tt_SocDescriptor::translate_coord_to(
    const tt_xy_pair core_location, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    return coordinate_manager->translate_coord_to(core_location, input_coord_system, target_coord_system);
}

tt_SocDescriptor::tt_SocDescriptor(
    std::string device_descriptor_path,
    const bool noc_translation_enabled,
    const HarvestingMasks harvesting_masks,
    const BoardType board_type,
    const bool is_chip_remote) :
    harvesting_masks(harvesting_masks) {
    std::ifstream fdesc(device_descriptor_path);
    if (fdesc.fail()) {
        throw std::runtime_error(
            fmt::format("Error: device descriptor file {} does not exist!", device_descriptor_path));
    }
    fdesc.close();

    YAML::Node device_descriptor_yaml = YAML::LoadFile(device_descriptor_path);

    auto grid_size_x = device_descriptor_yaml["grid"]["x_size"].as<int>();
    auto grid_size_y = device_descriptor_yaml["grid"]["y_size"].as<int>();
    load_core_descriptors_from_device_descriptor(device_descriptor_yaml);
    grid_size = tt_xy_pair(grid_size_x, grid_size_y);
    device_descriptor_file_path = device_descriptor_path;
    std::string arch_name_value = device_descriptor_yaml["arch_name"].as<std::string>();
    arch_name_value = trim(arch_name_value);
    arch = tt::arch_from_str(arch_name_value);
    load_soc_features_from_device_descriptor(device_descriptor_yaml);
    create_coordinate_manager(noc_translation_enabled, harvesting_masks, board_type, is_chip_remote);
}

int tt_SocDescriptor::get_num_dram_channels() const {
    int num_channels = 0;
    for (auto &dram_core : dram_cores) {
        if (dram_core.size() > 0) {
            num_channels++;
        }
    }
    return num_channels;
}

tt_xy_pair tt_SocDescriptor::get_core_for_dram_channel(int dram_chan, int subchannel) const {
    return this->dram_cores.at(dram_chan).at(subchannel);
}

CoreCoord tt_SocDescriptor::get_dram_core_for_channel(
    int dram_chan, int subchannel, const CoordSystem coord_system) const {
    const CoreCoord logical_dram_coord = CoreCoord(dram_chan, subchannel, CoreType::DRAM, CoordSystem::LOGICAL);
    return translate_coord_to(logical_dram_coord, coord_system);
}

CoreCoord tt_SocDescriptor::get_eth_core_for_channel(int eth_chan, const CoordSystem coord_system) const {
    const CoreCoord logical_eth_coord = CoreCoord(0, eth_chan, CoreType::ETH, CoordSystem::LOGICAL);
    return translate_coord_to(logical_eth_coord, coord_system);
}

std::string tt_SocDescriptor::get_soc_descriptor_path(
    tt::ARCH arch, const BoardType board_type, const bool is_chip_remote) {
    switch (arch) {
        case tt::ARCH::GRAYSKULL:
            // TODO: this path needs to be changed to point to soc descriptors outside of tests directory.
            return tt::umd::utils::get_abs_path("tests/soc_descs/grayskull_10x12.yaml");
        case tt::ARCH::WORMHOLE_B0:
            // TODO: this path needs to be changed to point to soc descriptors outside of tests directory.
            return tt::umd::utils::get_abs_path("tests/soc_descs/wormhole_b0_8x10.yaml");
        case tt::ARCH::BLACKHOLE: {
            auto chip_type = get_blackhole_chip_type(board_type, is_chip_remote);
            std::cout << "yeah get_soc_descriptor_path was called" << std::endl;
            return tt::umd::utils::get_abs_path(
                chip_type == BlackholeChipType::Type1 ? "tests/soc_descs/blackhole_140_arch_type1.yaml"
                                                      : "tests/soc_descs/blackhole_140_arch_type2.yaml");
        }
        default:
            throw std::runtime_error("Invalid architecture");
    }
}

void tt_SocDescriptor::get_cores_and_grid_size_from_coordinate_manager() {
    for (const auto &core_type :
         {CoreType::TENSIX, CoreType::DRAM, CoreType::ETH, CoreType::ARC, CoreType::PCIE, CoreType::ROUTER_ONLY}) {
        cores_map.insert({core_type, coordinate_manager->get_cores(core_type)});
        harvested_cores_map.insert({core_type, coordinate_manager->get_harvested_cores(core_type)});
        if (core_type == CoreType::ETH || core_type == CoreType::ROUTER_ONLY) {
            // Ethernet and Router cores aren't arranged in a grid.
            continue;
        }
        grid_size_map.insert({core_type, coordinate_manager->get_grid_size(core_type)});
        harvested_grid_size_map.insert({core_type, coordinate_manager->get_harvested_grid_size(core_type)});
    }

    const std::vector<CoreCoord> dram_cores = cores_map.at(CoreType::DRAM);
    const tt_xy_pair dram_grid_size = grid_size_map.at(CoreType::DRAM);

    dram_cores_core_coord.resize(dram_grid_size.x);
    for (size_t bank = 0; bank < dram_grid_size.x; bank++) {
        for (size_t noc_port = 0; noc_port < dram_grid_size.y; noc_port++) {
            dram_cores_core_coord[bank].push_back(dram_cores[bank * dram_grid_size.y + noc_port]);
        }
    }

    const std::vector<CoreCoord> harvested_dram_cores = harvested_cores_map.at(CoreType::DRAM);
    const tt_xy_pair harvested_dram_grid_size = harvested_grid_size_map.at(CoreType::DRAM);

    harvested_dram_cores_core_coord.resize(harvested_dram_grid_size.x);
    for (size_t bank = 0; bank < harvested_dram_grid_size.x; bank++) {
        for (size_t noc_port = 0; noc_port < harvested_dram_grid_size.y; noc_port++) {
            harvested_dram_cores_core_coord[bank].push_back(
                harvested_dram_cores[bank * harvested_dram_grid_size.y + noc_port]);
        }
    }
}

std::vector<CoreCoord> tt_SocDescriptor::translate_coordinates(
    const std::vector<CoreCoord> &physical_cores, const CoordSystem coord_system) const {
    std::vector<CoreCoord> translated_cores;
    for (const auto &core : physical_cores) {
        translated_cores.push_back(translate_coord_to(core, coord_system));
    }
    return translated_cores;
}

std::vector<tt::umd::CoreCoord> tt_SocDescriptor::get_cores(
    const CoreType core_type, const CoordSystem coord_system) const {
    auto cores_map_it = cores_map.find(core_type);
    if (coord_system != CoordSystem::PHYSICAL) {
        return translate_coordinates(cores_map_it->second, coord_system);
    }
    return cores_map_it->second;
}

std::vector<tt::umd::CoreCoord> tt_SocDescriptor::get_harvested_cores(
    const CoreType core_type, const CoordSystem coord_system) const {
    if (coord_system == CoordSystem::LOGICAL) {
        throw std::runtime_error("Harvested cores are not supported for logical coordinates");
    }
    auto harvested_cores_map_it = harvested_cores_map.find(core_type);
    if (coord_system != CoordSystem::PHYSICAL) {
        return translate_coordinates(harvested_cores_map_it->second, coord_system);
    }
    return harvested_cores_map_it->second;
}

std::vector<tt::umd::CoreCoord> tt_SocDescriptor::get_all_cores(const CoordSystem coord_system) const {
    std::vector<tt::umd::CoreCoord> all_cores;
    for (const auto &core_type :
         {CoreType::TENSIX, CoreType::DRAM, CoreType::ETH, CoreType::ARC, CoreType::PCIE, CoreType::ROUTER_ONLY}) {
        auto cores = get_cores(core_type, coord_system);
        all_cores.insert(all_cores.end(), cores.begin(), cores.end());
    }
    return all_cores;
}

std::vector<tt::umd::CoreCoord> tt_SocDescriptor::get_all_harvested_cores(const CoordSystem coord_system) const {
    std::vector<tt::umd::CoreCoord> all_harvested_cores;
    for (const auto &core_type :
         {CoreType::TENSIX, CoreType::DRAM, CoreType::ETH, CoreType::ARC, CoreType::PCIE, CoreType::ROUTER_ONLY}) {
        auto harvested_cores = get_harvested_cores(core_type, coord_system);
        all_harvested_cores.insert(all_harvested_cores.end(), harvested_cores.begin(), harvested_cores.end());
    }
    return all_harvested_cores;
}

tt_xy_pair tt_SocDescriptor::get_grid_size(const CoreType core_type) const { return grid_size_map.at(core_type); }

tt_xy_pair tt_SocDescriptor::get_harvested_grid_size(const CoreType core_type) const {
    return harvested_grid_size_map.at(core_type);
}

std::vector<std::vector<CoreCoord>> tt_SocDescriptor::get_dram_cores() const { return dram_cores_core_coord; }

std::vector<std::vector<CoreCoord>> tt_SocDescriptor::get_harvested_dram_cores() const {
    return harvested_dram_cores_core_coord;
}

uint32_t tt_SocDescriptor::get_num_eth_channels() const { return coordinate_manager->get_num_eth_channels(); }

uint32_t tt_SocDescriptor::get_num_harvested_eth_channels() const {
    return coordinate_manager->get_num_harvested_eth_channels();
}
