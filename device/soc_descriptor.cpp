// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/soc_descriptor.hpp"

#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>

#include "assert.hpp"
#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "utils.hpp"

// #include "l1_address_map.h"
extern bool umd_use_noc1;

namespace tt::umd {

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

tt_xy_pair SocDescriptor::calculate_grid_size(const std::vector<tt_xy_pair> &cores) {
    std::unordered_set<size_t> x;
    std::unordered_set<size_t> y;
    for (auto core : cores) {
        x.insert(core.x);
        y.insert(core.y);
    }
    return {x.size(), y.size()};
}

void SocDescriptor::write_coords(void *out, const CoreCoord &core) const {
    YAML::Emitter *emitter = static_cast<YAML::Emitter *>(out);

    if (core.x < grid_size.x && core.y < grid_size.y) {
        auto coords = translate_coord_to(core, CoordSystem::NOC0);
        *emitter << std::to_string(coords.x) + "-" + std::to_string(coords.y);
    }
}

void SocDescriptor::write_core_locations(void *out, const CoreType &core_type) const {
    for (const auto &core : get_cores(core_type)) {
        write_coords(out, core);
    }
}

void SocDescriptor::serialize_dram_cores(void *out, const std::vector<std::vector<CoreCoord>> &cores) const {
    YAML::Emitter *emitter = static_cast<YAML::Emitter *>(out);

    const uint32_t num_noc_ports = cores.empty() ? 0 : cores[0].size();

    for (const auto &dram_cores : cores) {
        // Insert the dram core if it's within the given grid.
        bool serialize_cores = true;

        for (const auto &dram_core : dram_cores) {
            if ((dram_core.x > grid_size.x) || (dram_core.y > grid_size.y)) {
                serialize_cores = false;
            }
        }
        if (serialize_cores) {
            int dram_count = 0;
            for (const auto &dram_core : dram_cores) {
                if (dram_count % num_noc_ports == 0) {
                    *emitter << YAML::BeginSeq;
                }
                if (dram_core.x < grid_size.x && dram_core.y < grid_size.y) {
                    write_coords(emitter, dram_core);
                }
                if (dram_count % num_noc_ports == num_noc_ports - 1) {
                    *emitter << YAML::EndSeq;
                }
                dram_count++;
            }
        }
    }
}

void SocDescriptor::create_coordinate_manager(const BoardType board_type, const uint8_t asic_location) {
    const tt_xy_pair dram_grid_size = tt_xy_pair(dram_cores.size(), dram_cores.empty() ? 0 : dram_cores[0].size());
    const tt_xy_pair arc_grid_size = SocDescriptor::calculate_grid_size(arc_cores);
    tt_xy_pair pcie_grid_size = SocDescriptor::calculate_grid_size(pcie_cores);

    std::vector<tt_xy_pair> dram_cores_unpacked;
    for (const auto &vec : dram_cores) {
        for (const auto &core : vec) {
            dram_cores_unpacked.push_back(core);
        }
    }

    // TODO: P100 has two types of boards, each using different PCI core.
    // Either have two separate enums or completely remove the check here.
    // PCIE harvesting mask 0x1 corresponds to (2, 0) and 0x2 corresponds to (11, 0).
    // if (board_type == BoardType::P100 && harvesting_masks.pcie_harvesting_mask != 0x1) {
    //     throw std::runtime_error("P100 card should always have PCIE core (2, 0) harvested.");
    // }

    if (board_type == BoardType::P150 && harvesting_masks.pcie_harvesting_mask != 0x2) {
        throw std::runtime_error("P150 card should always have PCIE core (11, 0) harvested.");
    }

    if (board_type == BoardType::P300 && asic_location == 0 && harvesting_masks.pcie_harvesting_mask != 0x2) {
        throw std::runtime_error("P300 card left chip should always have PCIE core (11, 0) harvested.");
    }

    if (board_type == BoardType::P300 && asic_location == 1 && harvesting_masks.pcie_harvesting_mask != 0x1) {
        throw std::runtime_error("P300 card right chip should always have PCIE core (2, 0) harvested.");
    }

    pcie_grid_size = SocDescriptor::calculate_grid_size(pcie_cores);

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
        router_cores,
        security_cores,
        l2cpu_cores,
        noc0_x_to_noc1_x,
        noc0_y_to_noc1_y);
    get_cores_and_grid_size_from_coordinate_manager();
}

CoreCoord SocDescriptor::translate_coord_to(const CoreCoord core_coord, const CoordSystem coord_system) const {
    return coordinate_manager->translate_coord_to(core_coord, coord_system);
}

CoreCoord SocDescriptor::get_coord_at(const tt_xy_pair core, const CoordSystem coord_system) const {
    return coordinate_manager->get_coord_at(core, coord_system);
}

CoreCoord SocDescriptor::translate_coord_to(
    const tt_xy_pair core_location, const CoordSystem input_coord_system, const CoordSystem target_coord_system) const {
    return coordinate_manager->translate_coord_to(core_location, input_coord_system, target_coord_system);
}

tt_xy_pair SocDescriptor::translate_chip_coord_to_translated(const CoreCoord core) const {
    // Since NOC1 and translated coordinate space overlaps for Tensix cores on Blackhole,
    // Tensix cores are always used in translated space. Other cores are used either in
    // NOC1 or translated space depending on the umd_use_noc1 flag.
    // On Wormhole Tensix can use NOC1 space if umd_use_noc1 is set to true.
    if (noc_translation_enabled && arch == tt::ARCH::BLACKHOLE) {
        return translate_coord_to(core, CoordSystem::TRANSLATED);
    }

    return translate_coord_to(core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
}

void SocDescriptor::load_core_descriptors_from_soc_desc_info(const SocDescriptorInfo &soc_desc_info) {
    auto worker_l1_size = soc_desc_info.worker_l1_size;
    auto eth_l1_size = soc_desc_info.eth_l1_size;

    for (const auto &arc_core : soc_desc_info.arc_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = arc_core;
        core_descriptor.type = CoreType::ARC;
        cores.insert({core_descriptor.coord, core_descriptor});
        arc_cores.push_back(core_descriptor.coord);
    }

    for (const auto &pcie_core : soc_desc_info.pcie_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = pcie_core;
        core_descriptor.type = CoreType::PCIE;
        cores.insert({core_descriptor.coord, core_descriptor});
        pcie_cores.push_back(core_descriptor.coord);
    }

    int current_dram_channel = 0;
    for (auto channel_it = soc_desc_info.dram_cores.begin(); channel_it != soc_desc_info.dram_cores.end();
         ++channel_it) {
        dram_cores.push_back({});
        auto &soc_dram_cores = dram_cores.at(dram_cores.size() - 1);
        const auto &dram_cores = (*channel_it);
        for (unsigned int i = 0; i < dram_cores.size(); i++) {
            const auto &dram_core = dram_cores.at(i);
            CoreDescriptor core_descriptor;
            core_descriptor.coord = dram_core;
            core_descriptor.type = CoreType::DRAM;
            cores.insert({core_descriptor.coord, core_descriptor});
            soc_dram_cores.push_back(core_descriptor.coord);
            dram_core_channel_map[core_descriptor.coord] = {current_dram_channel, i};
        }
        current_dram_channel++;
    }

    int current_ethernet_channel = 0;
    for (const auto &eth_core : soc_desc_info.eth_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = eth_core;
        core_descriptor.type = CoreType::ETH;
        core_descriptor.l1_size = eth_l1_size;
        cores.insert({core_descriptor.coord, core_descriptor});
        ethernet_cores.push_back(core_descriptor.coord);

        ethernet_core_channel_map[core_descriptor.coord] = current_ethernet_channel;
        current_ethernet_channel++;
    }

    std::set<int> worker_routing_coords_x;
    std::set<int> worker_routing_coords_y;
    for (const auto &tensix_core : soc_desc_info.tensix_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = tensix_core;
        core_descriptor.type = CoreType::WORKER;
        core_descriptor.l1_size = worker_l1_size;
        cores.insert({core_descriptor.coord, core_descriptor});
        workers.push_back(core_descriptor.coord);
        worker_routing_coords_x.insert(core_descriptor.coord.x);
        worker_routing_coords_y.insert(core_descriptor.coord.y);
    }

    worker_grid_size = tt_xy_pair(worker_routing_coords_x.size(), worker_routing_coords_y.size());

    for (const auto &router_core : soc_desc_info.router_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = router_core;
        core_descriptor.type = CoreType::ROUTER_ONLY;
        cores.insert({core_descriptor.coord, core_descriptor});
        router_cores.push_back(core_descriptor.coord);
    }

    for (const auto &security_core : soc_desc_info.security_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = security_core;
        core_descriptor.type = CoreType::SECURITY;
        cores.insert({core_descriptor.coord, core_descriptor});
        security_cores.push_back(core_descriptor.coord);
    }

    for (const auto &l2cpu_core : soc_desc_info.l2cpu_cores) {
        CoreDescriptor core_descriptor;
        core_descriptor.coord = l2cpu_core;
        core_descriptor.type = CoreType::L2CPU;
        cores.insert({core_descriptor.coord, core_descriptor});
        l2cpu_cores.push_back(core_descriptor.coord);
    }

    noc0_x_to_noc1_x = soc_desc_info.noc0_x_to_noc1_x;
    noc0_y_to_noc1_y = soc_desc_info.noc0_y_to_noc1_y;
}

void SocDescriptor::load_soc_features_from_soc_desc_info(const SocDescriptorInfo &soc_desc_info) {
    worker_l1_size = soc_desc_info.worker_l1_size;
    eth_l1_size = soc_desc_info.eth_l1_size;
    dram_bank_size = soc_desc_info.dram_bank_size;
}

SocDescriptorInfo SocDescriptor::get_soc_descriptor_info(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            return SocDescriptorInfo{
                .arch = tt::ARCH::WORMHOLE_B0,
                .grid_size = wormhole::GRID_SIZE,
                .tensix_cores = wormhole::TENSIX_CORES_NOC0,
                .dram_cores = wormhole::DRAM_CORES_NOC0,
                .eth_cores = wormhole::ETH_CORES_NOC0,
                .arc_cores = wormhole::ARC_CORES_NOC0,
                .pcie_cores = wormhole::PCIE_CORES_NOC0,
                .router_cores = wormhole::ROUTER_CORES_NOC0,
                .security_cores = wormhole::SECURITY_CORES_NOC0,
                .l2cpu_cores = wormhole::L2CPU_CORES_NOC0,
                .worker_l1_size = wormhole::TENSIX_L1_SIZE,
                .eth_l1_size = wormhole::ETH_L1_SIZE,
                .dram_bank_size = wormhole::DRAM_BANK_SIZE,
                .noc0_x_to_noc1_x = wormhole::NOC0_X_TO_NOC1_X,
                .noc0_y_to_noc1_y = wormhole::NOC0_Y_TO_NOC1_Y};
            break;
        }
        case tt::ARCH::BLACKHOLE: {
            return SocDescriptorInfo{
                .arch = tt::ARCH::BLACKHOLE,
                .grid_size = blackhole::GRID_SIZE,
                .tensix_cores = blackhole::TENSIX_CORES_NOC0,
                .dram_cores = blackhole::DRAM_CORES_NOC0,
                .eth_cores = blackhole::ETH_CORES_NOC0,
                .arc_cores = blackhole::ARC_CORES_NOC0,
                .pcie_cores = blackhole::PCIE_CORES_NOC0,
                .router_cores = blackhole::ROUTER_CORES_NOC0,
                .security_cores = blackhole::SECURITY_CORES_NOC0,
                .l2cpu_cores = blackhole::L2CPU_CORES_NOC0,
                .worker_l1_size = blackhole::TENSIX_L1_SIZE,
                .eth_l1_size = blackhole::ETH_L1_SIZE,
                .dram_bank_size = blackhole::DRAM_BANK_SIZE,
                .noc0_x_to_noc1_x = blackhole::NOC0_X_TO_NOC1_X,
                .noc0_y_to_noc1_y = blackhole::NOC0_Y_TO_NOC1_Y};
            break;
        }
        case tt::ARCH::QUASAR: {
            return SocDescriptorInfo{
                .arch = tt::ARCH::QUASAR,
                .grid_size = grendel::GRID_SIZE,
                .tensix_cores = grendel::TENSIX_CORES_NOC0,
                .dram_cores = grendel::DRAM_CORES_NOC0,
                .eth_cores = grendel::ETH_CORES_NOC0,
                .arc_cores = grendel::ARC_CORES_NOC0,
                .pcie_cores = grendel::PCIE_CORES_NOC0,
                .router_cores = grendel::ROUTER_CORES_NOC0,
                .security_cores = grendel::SECURITY_CORES_NOC0,
                .l2cpu_cores = grendel::L2CPU_CORES_NOC0,
                .worker_l1_size = grendel::TENSIX_L1_SIZE,
                .eth_l1_size = grendel::ETH_L1_SIZE,
                .dram_bank_size = grendel::DRAM_BANK_SIZE,
                .noc0_x_to_noc1_x = grendel::NOC0_X_TO_NOC1_X,
                .noc0_y_to_noc1_y = grendel::NOC0_Y_TO_NOC1_Y};
            break;
        }
        default:
            throw std::runtime_error("Invalid architecture for creating SocDescriptorInfo.");
    }
}

SocDescriptor::SocDescriptor(const tt::ARCH arch_soc, ChipInfo chip_info) :
    noc_translation_enabled(chip_info.noc_translation_enabled), harvesting_masks(chip_info.harvesting_masks) {
    SocDescriptorInfo soc_desc_info = SocDescriptor::get_soc_descriptor_info(arch_soc);
    load_from_soc_desc_info(soc_desc_info);
    create_coordinate_manager(chip_info.board_type, chip_info.asic_location);
}

void SocDescriptor::load_from_soc_desc_info(const SocDescriptorInfo &soc_desc_info) {
    arch = soc_desc_info.arch;
    grid_size = soc_desc_info.grid_size;
    load_core_descriptors_from_soc_desc_info(soc_desc_info);
    load_soc_features_from_soc_desc_info(soc_desc_info);
}

std::vector<tt_xy_pair> SocDescriptor::convert_to_tt_xy_pair(const std::vector<std::string> &core_strings) {
    std::vector<tt_xy_pair> core_pairs;
    for (const auto &core_string : core_strings) {
        core_pairs.push_back(format_node(core_string));
    }

    return core_pairs;
}

tt::ARCH SocDescriptor::get_arch_from_soc_descriptor_path(const std::string &soc_descriptor_path) {
    YAML::Node device_descriptor_yaml = YAML::LoadFile(soc_descriptor_path);
    return tt::arch_from_str(device_descriptor_yaml["arch_name"].as<std::string>());
}

tt_xy_pair SocDescriptor::get_grid_size_from_soc_descriptor_path(const std::string &soc_descriptor_path) {
    YAML::Node device_descriptor_yaml = YAML::LoadFile(soc_descriptor_path);
    return tt_xy_pair(
        device_descriptor_yaml["grid"]["x_size"].as<int>(), device_descriptor_yaml["grid"]["y_size"].as<int>());
}

std::vector<std::vector<tt_xy_pair>> SocDescriptor::convert_dram_cores_from_yaml(
    YAML::Node &device_descriptor_yaml, const std::string &dram_core) {
    std::vector<std::vector<tt_xy_pair>> dram_cores;
    for (auto channel_it = device_descriptor_yaml[dram_core].begin();
         channel_it != device_descriptor_yaml[dram_core].end();
         ++channel_it) {
        dram_cores.push_back(convert_to_tt_xy_pair((*channel_it).as<std::vector<std::string>>()));
    }

    return dram_cores;
}

void SocDescriptor::load_from_yaml(YAML::Node &device_descriptor_yaml) {
    SocDescriptorInfo soc_desc_info;

    soc_desc_info.grid_size = tt_xy_pair(
        device_descriptor_yaml["grid"]["x_size"].as<int>(), device_descriptor_yaml["grid"]["y_size"].as<int>());

    std::string arch_name_value = device_descriptor_yaml["arch_name"].as<std::string>();
    arch_name_value = trim(arch_name_value);
    arch = tt::arch_from_str(arch_name_value);
    soc_desc_info.arch = arch;

    soc_desc_info.tensix_cores = SocDescriptor::convert_to_tt_xy_pair(
        device_descriptor_yaml["functional_workers"].as<std::vector<std::string>>());
    soc_desc_info.dram_cores = SocDescriptor::convert_dram_cores_from_yaml(device_descriptor_yaml, "dram");
    soc_desc_info.pcie_cores =
        SocDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["pcie"].as<std::vector<std::string>>());
    soc_desc_info.eth_cores =
        SocDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["eth"].as<std::vector<std::string>>());
    soc_desc_info.arc_cores =
        SocDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["arc"].as<std::vector<std::string>>());
    soc_desc_info.router_cores =
        SocDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["router_only"].as<std::vector<std::string>>());
    if (device_descriptor_yaml["l2cpu"].IsDefined()) {
        soc_desc_info.l2cpu_cores =
            SocDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["l2cpu"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["security"].IsDefined()) {
        soc_desc_info.security_cores =
            SocDescriptor::convert_to_tt_xy_pair(device_descriptor_yaml["security"].as<std::vector<std::string>>());
    }

    if (device_descriptor_yaml["noc0_x_to_noc1_x"].IsDefined()) {
        soc_desc_info.noc0_x_to_noc1_x = device_descriptor_yaml["noc0_x_to_noc1_x"].as<std::vector<uint32_t>>();
        soc_desc_info.noc0_y_to_noc1_y = device_descriptor_yaml["noc0_y_to_noc1_y"].as<std::vector<uint32_t>>();
    }

    soc_desc_info.worker_l1_size = device_descriptor_yaml["worker_l1_size"].as<uint32_t>();
    soc_desc_info.eth_l1_size = device_descriptor_yaml["eth_l1_size"].as<uint32_t>();
    soc_desc_info.dram_bank_size = device_descriptor_yaml["dram_bank_size"].as<uint64_t>();

    // Inlcude harvested cores directly in SocDescriptor if available.
    if (device_descriptor_yaml["harvested_workers"].IsDefined()) {
        harvested_workers = SocDescriptor::convert_to_tt_xy_pair(
            device_descriptor_yaml["harvested_workers"].as<std::vector<std::string>>());
    }
    if (device_descriptor_yaml["harvested_eth"].IsDefined()) {
        harvested_ethernet_cores = SocDescriptor::convert_to_tt_xy_pair(
            device_descriptor_yaml["harvested_eth"].as<std::vector<std::string>>());
    }
    if (device_descriptor_yaml["harvested_dram"].IsDefined()) {
        harvested_dram_cores = SocDescriptor::convert_dram_cores_from_yaml(device_descriptor_yaml, "harvested_dram");
    }

    load_from_soc_desc_info(soc_desc_info);
}

SocDescriptor::SocDescriptor(const std::string &device_descriptor_path, ChipInfo chip_info) :
    noc_translation_enabled(chip_info.noc_translation_enabled), harvesting_masks(chip_info.harvesting_masks) {
    std::ifstream fdesc(device_descriptor_path);
    if (fdesc.fail()) {
        throw std::runtime_error(
            fmt::format("Error: device descriptor file {} does not exist!", device_descriptor_path));
    }
    fdesc.close();

    YAML::Node device_descriptor_yaml = YAML::LoadFile(device_descriptor_path);

    device_descriptor_file_path = device_descriptor_path;
    load_from_yaml(device_descriptor_yaml);

    create_coordinate_manager(chip_info.board_type, chip_info.asic_location);
}

int SocDescriptor::get_num_dram_channels() const { return get_grid_size(CoreType::DRAM).x; }

CoreCoord SocDescriptor::get_dram_core_for_channel(
    int dram_chan, int subchannel, const CoordSystem coord_system) const {
    const CoreCoord logical_dram_coord = CoreCoord(dram_chan, subchannel, CoreType::DRAM, CoordSystem::LOGICAL);
    return translate_coord_to(logical_dram_coord, coord_system);
}

std::unordered_set<CoreCoord> SocDescriptor::translate_coords_to(
    const std::unordered_set<CoreCoord> &core_coords, const CoordSystem coord_system) const {
    std::unordered_set<CoreCoord> translated_cores;
    for (const auto &core : core_coords) {
        translated_cores.insert(translate_coord_to(core, coord_system));
    }
    return translated_cores;
}

std::unordered_set<tt_xy_pair> SocDescriptor::translate_coords_to_xy_pair(
    const std::unordered_set<CoreCoord> &core_coords, const CoordSystem coord_system) const {
    std::unordered_set<tt_xy_pair> translated_xy_pairs;
    for (const auto &core : core_coords) {
        CoreCoord translated_core = translate_coord_to(core, coord_system);
        translated_xy_pairs.insert({translated_core.x, translated_core.y});
    }
    return translated_xy_pairs;
}

std::unordered_set<CoreCoord> SocDescriptor::get_eth_cores_for_channels(
    const std::set<uint32_t> &eth_channels, const CoordSystem coord_system) const {
    std::unordered_set<CoreCoord> eth_cores;
    for (uint32_t channel : eth_channels) {
        eth_cores.insert(get_eth_core_for_channel(channel, coord_system));
    }
    return eth_cores;
}

std::unordered_set<tt_xy_pair> SocDescriptor::get_eth_xy_pairs_for_channels(
    const std::set<uint32_t> &eth_channels, const CoordSystem coord_system) const {
    std::unordered_set<tt_xy_pair> eth_xy_pairs;
    for (uint32_t channel : eth_channels) {
        CoreCoord eth_core = get_eth_core_for_channel(channel, coord_system);
        eth_xy_pairs.insert({eth_core.x, eth_core.y});
    }
    return eth_xy_pairs;
}

uint32_t SocDescriptor::get_eth_channel_for_core(const CoreCoord &core_coord, const CoordSystem coord_system) const {
    return translate_coord_to(core_coord, CoordSystem::LOGICAL).y;
}

std::pair<int, int> SocDescriptor::get_dram_channel_for_core(
    const CoreCoord &core_coord, const CoordSystem coord_system) const {
    auto logical_core = translate_coord_to(core_coord, CoordSystem::LOGICAL);
    return std::make_pair(logical_core.x, logical_core.y);
}

CoreCoord SocDescriptor::get_eth_core_for_channel(uint32_t eth_chan, const CoordSystem coord_system) const {
    const CoreCoord logical_eth_coord = CoreCoord(0, eth_chan, CoreType::ETH, CoordSystem::LOGICAL);
    return translate_coord_to(logical_eth_coord, coord_system);
}

std::string SocDescriptor::serialize() const {
    YAML::Emitter out;

    out << YAML::BeginMap;

    out << YAML::Key << "grid" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "x_size" << YAML::Value << grid_size.x;
    out << YAML::Key << "y_size" << YAML::Value << grid_size.y;
    out << YAML::EndMap;

    out << YAML::Key << "arc" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::ARC);
    out << YAML::EndSeq;

    out << YAML::Key << "pcie" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::PCIE);
    out << YAML::EndSeq;

    out << YAML::Key << "harvested_dram" << YAML::Value << YAML::BeginSeq;
    serialize_dram_cores(&out, get_harvested_dram_cores());
    out << YAML::EndSeq;

    out << YAML::Key << "dram" << YAML::Value << YAML::BeginSeq;
    serialize_dram_cores(&out, get_dram_cores());
    out << YAML::EndSeq;

    out << YAML::Key << "harvested_eth" << YAML::Value << YAML::BeginSeq;
    for (const auto &eth : get_harvested_cores(CoreType::ETH)) {
        write_coords(&out, eth);
    }
    out << YAML::EndSeq;

    out << YAML::Key << "eth" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::ETH);
    out << YAML::EndSeq;

    out << YAML::Key << "harvested_workers" << YAML::Value << YAML::BeginSeq;
    for (const auto &worker : get_harvested_cores(CoreType::TENSIX)) {
        write_coords(&out, worker);
    }
    out << YAML::EndSeq;

    out << YAML::Key << "functional_workers" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::TENSIX);
    out << YAML::EndSeq;

    out << YAML::Key << "router_only" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::ROUTER_ONLY);
    out << YAML::EndSeq;

    out << YAML::Key << "security" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::SECURITY);
    out << YAML::EndSeq;

    out << YAML::Key << "l2cpu" << YAML::Value << YAML::BeginSeq;
    write_core_locations(&out, CoreType::L2CPU);
    out << YAML::EndSeq;

    // Fill in the rest that are static to our device.
    out << YAML::Key << "worker_l1_size" << YAML::Value << worker_l1_size;
    out << YAML::Key << "dram_bank_size" << YAML::Value << dram_bank_size;
    out << YAML::Key << "eth_l1_size" << YAML::Value << eth_l1_size;
    out << YAML::Key << "arch_name" << YAML::Value << tt::arch_to_str(arch);

    out << YAML::Key << "features" << YAML::Value << YAML::BeginMap;

    out << YAML::Key << "noc" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "translation_id_enabled" << YAML::Value << true;
    out << YAML::EndMap;

    out << YAML::Key << "unpacker" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << unpacker_version;
    out << YAML::Key << "inline_srca_trans_without_srca_trans_instr" << YAML::Value << true;
    out << YAML::EndMap;

    out << YAML::Key << "math" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "dst_size_alignment" << YAML::Value << dst_size_alignment;
    out << YAML::EndMap;

    out << YAML::Key << "packer" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << packer_version;
    out << YAML::EndMap;

    out << YAML::Key << "overlay" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << overlay_version;
    out << YAML::EndMap;

    out << YAML::EndMap;

    return out.c_str();
}

std::filesystem::path SocDescriptor::serialize_to_file(const std::filesystem::path &dest_file) const {
    std::filesystem::path file_path = dest_file;
    if (file_path.empty()) {
        file_path = get_default_soc_descriptor_file_path();
    }
    std::ofstream file(file_path);
    file << serialize();
    file.close();
    return file_path;
}

std::filesystem::path SocDescriptor::get_default_soc_descriptor_file_path() {
    std::filesystem::path temp_path = std::filesystem::temp_directory_path();
    std::string soc_path_dir_template = temp_path / "umd_XXXXXX";
    std::filesystem::path soc_path_dir = mkdtemp(soc_path_dir_template.data());
    std::filesystem::path soc_path = soc_path_dir / "soc_descriptor.yaml";

    return soc_path;
}

void SocDescriptor::get_cores_and_grid_size_from_coordinate_manager() {
    const tt_xy_pair empty = {0, 0};
    for (const auto &core_type :
         {CoreType::TENSIX,
          CoreType::DRAM,
          CoreType::ETH,
          CoreType::ARC,
          CoreType::PCIE,
          CoreType::ROUTER_ONLY,
          CoreType::SECURITY,
          CoreType::L2CPU}) {
        cores_map.insert({core_type, coordinate_manager->get_cores(core_type)});
        harvested_cores_map.insert({core_type, coordinate_manager->get_harvested_cores(core_type)});
        if (core_type == CoreType::ETH || core_type == CoreType::ROUTER_ONLY || core_type == CoreType::SECURITY ||
            core_type == CoreType::L2CPU) {
            // Ethernet and Router cores aren't arranged in a grid, initializing as empty.
            grid_size_map.insert({core_type, empty});
            harvested_grid_size_map.insert({core_type, empty});
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

std::vector<CoreCoord> SocDescriptor::translate_coordinates(
    const std::vector<CoreCoord> &noc0_cores, const CoordSystem coord_system) const {
    std::vector<CoreCoord> translated_cores;
    for (const auto &core : noc0_cores) {
        translated_cores.push_back(translate_coord_to(core, coord_system));
    }
    return translated_cores;
}

std::vector<CoreCoord> SocDescriptor::get_cores(
    const CoreType core_type, const CoordSystem coord_system, std::optional<uint32_t> channel) const {
    auto cores_map_it = cores_map.find(core_type);
    std::vector<CoreCoord> cores = cores_map_it->second;

    // Filter cores by channel if specified.
    // At this time, only applicable for DRAM cores.
    if (channel.has_value()) {
        TT_ASSERT(core_type == CoreType::DRAM, "Core type must be DRAM when setting channel.");
        TT_ASSERT(channel.value() < get_num_dram_channels(), "Channel value exceeds number of DRAM channels.");
        std::vector<CoreCoord> filtered_cores;
        for (const auto &core : cores) {
            auto logical_core = translate_coord_to(core, CoordSystem::LOGICAL);
            if (logical_core.y == channel.value()) {
                filtered_cores.push_back(core);
            }
        }
        cores = filtered_cores;
    }

    if (coord_system != CoordSystem::NOC0) {
        return translate_coordinates(cores, coord_system);
    }
    return cores;
}

std::vector<CoreCoord> SocDescriptor::get_harvested_cores(
    const CoreType core_type, const CoordSystem coord_system) const {
    if (coord_system == CoordSystem::LOGICAL) {
        throw std::runtime_error("Harvested cores are not supported for logical coordinates");
    }
    auto harvested_cores_map_it = harvested_cores_map.find(core_type);
    if (coord_system != CoordSystem::NOC0) {
        return translate_coordinates(harvested_cores_map_it->second, coord_system);
    }
    return harvested_cores_map_it->second;
}

std::vector<CoreCoord> SocDescriptor::get_all_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> all_cores;
    for (const auto &core_type :
         {CoreType::TENSIX,
          CoreType::DRAM,
          CoreType::ETH,
          CoreType::ARC,
          CoreType::PCIE,
          CoreType::ROUTER_ONLY,
          CoreType::SECURITY,
          CoreType::L2CPU}) {
        auto cores = get_cores(core_type, coord_system);
        all_cores.insert(all_cores.end(), cores.begin(), cores.end());
    }
    return all_cores;
}

std::vector<CoreCoord> SocDescriptor::get_all_harvested_cores(const CoordSystem coord_system) const {
    std::vector<CoreCoord> all_harvested_cores;
    for (const auto &core_type :
         {CoreType::TENSIX,
          CoreType::DRAM,
          CoreType::ETH,
          CoreType::ARC,
          CoreType::PCIE,
          CoreType::ROUTER_ONLY,
          CoreType::SECURITY,
          CoreType::L2CPU}) {
        auto harvested_cores = get_harvested_cores(core_type, coord_system);
        all_harvested_cores.insert(all_harvested_cores.end(), harvested_cores.begin(), harvested_cores.end());
    }
    return all_harvested_cores;
}

tt_xy_pair SocDescriptor::get_grid_size(const CoreType core_type) const { return grid_size_map.at(core_type); }

tt_xy_pair SocDescriptor::get_harvested_grid_size(const CoreType core_type) const {
    return harvested_grid_size_map.at(core_type);
}

std::vector<std::vector<CoreCoord>> SocDescriptor::get_dram_cores() const { return dram_cores_core_coord; }

std::vector<std::vector<CoreCoord>> SocDescriptor::get_harvested_dram_cores() const {
    return harvested_dram_cores_core_coord;
}

uint32_t SocDescriptor::get_num_eth_channels() const { return coordinate_manager->get_num_eth_channels(); }

uint32_t SocDescriptor::get_num_harvested_eth_channels() const {
    return coordinate_manager->get_num_harvested_eth_channels();
}

}  // namespace tt::umd
