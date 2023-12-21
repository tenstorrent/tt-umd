// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0


#include "tt_cluster_descriptor.h"

#include <fstream>
#include <memory>
#include <sstream> 

#include "common/logger.hpp"
#include "yaml-cpp/yaml.h"

using namespace tt;
bool tt_ClusterDescriptor::ethernet_core_has_active_ethernet_link(chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const {
    return this->ethernet_connections.find(local_chip) != this->ethernet_connections.end() &&
           this->ethernet_connections.at(local_chip).find(local_ethernet_channel) != this->ethernet_connections.at(local_chip).end();
}

std::tuple<chip_id_t, ethernet_channel_t> tt_ClusterDescriptor::get_chip_and_channel_of_remote_ethernet_core(
    chip_id_t local_chip, ethernet_channel_t local_ethernet_channel) const {
    std::vector<std::tuple<ethernet_channel_t, ethernet_channel_t>> directly_connected_channels = {};
    if (this->enabled_active_chips.find(local_chip) == this->enabled_active_chips.end() ||
        this->ethernet_connections.at(local_chip).find(local_ethernet_channel) ==
            this->ethernet_connections.at(local_chip).end()) {
        return {};
    }

    const auto &[connected_chip, connected_channel] =
        this->ethernet_connections.at(local_chip).at(local_ethernet_channel);
    if (this->enabled_active_chips.find(connected_chip) == this->enabled_active_chips.end()) {
        return {};
    } else {
        return {connected_chip, connected_channel};
    }
}

// NOTE: It might be worthwhile to precompute this for every pair of directly connected chips, depending on how extensively router needs to use it
std::vector<std::tuple<ethernet_channel_t, ethernet_channel_t>> tt_ClusterDescriptor::get_directly_connected_ethernet_channels_between_chips(const chip_id_t &first, const chip_id_t &second) const {
    std::vector<std::tuple<ethernet_channel_t, ethernet_channel_t>> directly_connected_channels = {};
    if (this->enabled_active_chips.find(first) == this->enabled_active_chips.end() || this->enabled_active_chips.find(second) == this->enabled_active_chips.end()) {
        return {};
    }

    for (const auto &[first_ethernet_channel, connected_chip_and_channel] : this->ethernet_connections.at(first)) {
        if (std::get<0>(connected_chip_and_channel) == second) {
            directly_connected_channels.push_back({first_ethernet_channel, std::get<1>(connected_chip_and_channel)});
        }
    }

    return directly_connected_channels;
}

bool tt_ClusterDescriptor::channels_are_directly_connected(const chip_id_t &first, const ethernet_channel_t &first_channel, const chip_id_t &second, const ethernet_channel_t &second_channel) const {
    if (this->enabled_active_chips.find(first) == this->enabled_active_chips.end() || this->enabled_active_chips.find(second) == this->enabled_active_chips.end()) {
        return false;
    }

    if (this->ethernet_connections.at(first).find(first_channel) == this->ethernet_connections.at(first).end()) {
        return false;
    }

    const auto &[connected_chip, connected_channel] = this->ethernet_connections.at(first).at(first_channel);
    return connected_chip == second && connected_channel == second_channel;   
}

// const eth_coord_t tt_ClusterDescriptor::get_chip_xy(const chip_id_t &chip_id) const {
//     // For now we only support a 1D cluster, so the mapping is trivial (where the chip ID is the x value of the xy
//     location) return eth_coord_t(chip_id, 0, 0, 0);
// }

// const chip_id_t tt_ClusterDescriptor::get_chip_id_at_location(const eth_coord_t &chip_location) const {
//     // For now we only support a 1D cluster, so the mapping is trivial (where the chip ID is the x value of the xy
//     location) return chip_location.x;
// }

bool tt_ClusterDescriptor::is_chip_mmio_capable(const chip_id_t &chip_id) const {
    return this->chips_with_mmio.find(chip_id) != this->chips_with_mmio.end();
}

int get_ethernet_link_distance(const eth_coord_t &location_a, const eth_coord_t &location_b) {
    // eth_coord_t: x, y, rack, shelf
    int x_distance = std::abs(std::get<0>(location_a) - std::get<0>(location_b));
    int y_distance = std::abs(std::get<1>(location_a) - std::get<1>(location_b));
    
    int chips_per_shelf = 32;
    int shelves_per_rack = 8;
    // chips on separate shelves are much further apart then two chips on same shelf
    int shelf_distance = (std::abs(std::get<3>(location_a) - std::get<3>(location_b))) * chips_per_shelf;
    int rack_distance = (std::abs(std::get<2>(location_a) - std::get<2>(location_b))) * (shelves_per_rack * chips_per_shelf);
    return x_distance + y_distance + rack_distance + shelf_distance;
}

chip_id_t tt_ClusterDescriptor::get_closest_mmio_capable_chip(const chip_id_t &chip) const {
    int min_distance = std::numeric_limits<int>::max();
    chip_id_t closest_chip = chip;
    for (const auto &pair : this->chips_with_mmio) {
        const chip_id_t &c = pair.first;
        eth_coord_t c_eth_coord = this->chip_locations.at(c);
        eth_coord_t chip_eth_coord = this->chip_locations.at(chip);
        int distance = get_ethernet_link_distance(c_eth_coord, chip_eth_coord);
        if (distance < min_distance) {
            min_distance = distance;
            closest_chip = c;
        }
    }
    log_assert(is_chip_mmio_capable(closest_chip), "Closest MMIO chip must be MMIO capable");

    return closest_chip;

}

std::unique_ptr<tt_ClusterDescriptor> tt_ClusterDescriptor::create_from_yaml(const std::string &cluster_descriptor_file_path) {
    std::unique_ptr<tt_ClusterDescriptor> desc = std::unique_ptr<tt_ClusterDescriptor>(new tt_ClusterDescriptor());

    std::ifstream fdesc(cluster_descriptor_file_path);
    if (fdesc.fail()) {
        throw std::runtime_error("Error: cluster connectivity descriptor file " + cluster_descriptor_file_path + " does not exist!");
    }
    fdesc.close();

    YAML::Node yaml = YAML::LoadFile(cluster_descriptor_file_path);
    tt_ClusterDescriptor::load_chips_from_connectivity_descriptor(yaml, *desc);
    tt_ClusterDescriptor::load_ethernet_connections_from_connectivity_descriptor(yaml, *desc);
    tt_ClusterDescriptor::load_harvesting_information(yaml, *desc);
    desc->enable_all_devices();

    return desc;
}

std::unique_ptr<tt_ClusterDescriptor> tt_ClusterDescriptor::create_for_grayskull_cluster(
    const std::set<chip_id_t> &logical_mmio_device_ids,
    const std::vector<chip_id_t> &physical_mmio_device_ids) {
    std::unique_ptr<tt_ClusterDescriptor> desc = std::unique_ptr<tt_ClusterDescriptor>(new tt_ClusterDescriptor());

    // Some users need not care about physical ids, can provide empty set.
    auto use_physical_ids                   = physical_mmio_device_ids.size() ? true : false;
    auto largest_workload_logical_device_id = *logical_mmio_device_ids.rbegin(); // Last element in ordered set.
    auto num_available_physical_devices     = physical_mmio_device_ids.size();
    auto required_physical_devices          = largest_workload_logical_device_id + 1;

    log_debug(tt::LogSiliconDriver, "{} - use_physical_ids: {} largest_workload_logical_device_id: {} num_available_physical_devices: {} required_physical_devices: {}",
        __FUNCTION__, use_physical_ids, largest_workload_logical_device_id, num_available_physical_devices, required_physical_devices);

    log_assert(!use_physical_ids || num_available_physical_devices >= required_physical_devices,
        "Insufficient silicon devices. Workload requires device_id: {} (ie. {} devices) but only {} present",
        largest_workload_logical_device_id, required_physical_devices, num_available_physical_devices);

    // All Grayskull devices are MMIO mapped so physical_mmio_device_ids correspond to all available devices
    for (auto &logical_id : logical_mmio_device_ids) {
        auto physical_id = use_physical_ids ? physical_mmio_device_ids.at(logical_id) : -1;
        desc->chips_with_mmio.insert({logical_id, physical_id});
        desc->all_chips.insert(logical_id);
        eth_coord_t chip_location{logical_id, 0, 0, 0};
        desc->chip_locations.insert({logical_id, chip_location});
        log_debug(tt::LogSiliconDriver, "{} - adding logical: {} => physical: {}", __FUNCTION__, logical_id, physical_id);
    }

    desc->enable_all_devices();

    return desc;
}

std::set<chip_id_t> get_sequential_chip_id_set(int num_chips) {
    std::set<chip_id_t> chip_ids;
    for (int i = 0; i < num_chips; ++i) {
        chip_ids.insert(static_cast<chip_id_t>(i));
    }
    return chip_ids;
}

void tt_ClusterDescriptor::load_ethernet_connections_from_connectivity_descriptor(YAML::Node &yaml, tt_ClusterDescriptor &desc) {
    log_assert(yaml["ethernet_connections"].IsSequence(), "Invalid YAML");
    for (YAML::Node &connected_endpoints : yaml["ethernet_connections"].as<std::vector<YAML::Node>>()) {
        log_assert(connected_endpoints.IsSequence(), "Invalid YAML");

        std::vector<YAML::Node> endpoints = connected_endpoints.as<std::vector<YAML::Node>>();
        log_assert(endpoints.size() == 2, "Currently ethernet cores can only connect to one other ethernet endpoint");

        int chip_0 = endpoints.at(0)["chip"].as<int>();
        int channel_0 = endpoints.at(0)["chan"].as<int>();
        int chip_1 = endpoints.at(1)["chip"].as<int>();
        int channel_1 = endpoints.at(1)["chan"].as<int>();
        if (desc.ethernet_connections[chip_0].find(channel_0) != desc.ethernet_connections[chip_0].end()) {
            log_assert(
                (std::get<0>(desc.ethernet_connections[chip_0][channel_0]) == chip_1) &&
                    (std::get<1>(desc.ethernet_connections[chip_0][channel_0]) == channel_1),
                "Duplicate eth connection found in cluster desc yaml");
        } else {
            desc.ethernet_connections[chip_0][channel_0] = {chip_1, channel_1};
        }
        if (desc.ethernet_connections[chip_1].find(channel_1) != desc.ethernet_connections[chip_0].end()) {
            log_assert(
                (std::get<0>(desc.ethernet_connections[chip_1][channel_1]) == chip_0) &&
                    (std::get<1>(desc.ethernet_connections[chip_1][channel_1]) == channel_0),
                "Duplicate eth connection found in cluster desc yaml");
        } else {
            desc.ethernet_connections[chip_1][channel_1] = {chip_0, channel_0};
        }
    }

    log_debug(LogSiliconDriver, "Ethernet Connectivity Descriptor:");
    for (const auto &[chip, chan_to_chip_chan_map] : desc.ethernet_connections) {
        for (const auto &[chan, chip_and_chan] : chan_to_chip_chan_map) {
            log_debug(LogSiliconDriver, "\tchip: {}, chan: {}  <-->  chip: {}, chan: {}", chip, chan, std::get<0>(chip_and_chan), std::get<1>(chip_and_chan));
        }
    }
}

void tt_ClusterDescriptor::load_chips_from_connectivity_descriptor(YAML::Node &yaml, tt_ClusterDescriptor &desc) {
    for (YAML::const_iterator node = yaml["chips"].begin(); node != yaml["chips"].end(); ++node) {
        chip_id_t chip_id = node->first.as<int>();
        std::vector<int> chip_rack_coords = node->second.as<std::vector<int>>();
        log_assert(chip_rack_coords.size() == 4, "Galaxy (x, y, rack, shelf) coords must be size 4");
        eth_coord_t chip_location{
            chip_rack_coords.at(0), chip_rack_coords.at(1), chip_rack_coords.at(2), chip_rack_coords.at(3)};
        
        desc.chip_locations.insert({chip_id, chip_location});
        desc.all_chips.insert(chip_id);
    }
    
    for(const auto& chip : yaml["chips_with_mmio"]) {
        if(chip.IsMap()) {
            const auto &chip_map = chip.as<std::map<chip_id_t, chip_id_t>>().begin();
            desc.chips_with_mmio.insert({chip_map->first, chip_map->second});
        }
        else {
            const auto &chip_val = chip.as<int>();
            desc.chips_with_mmio.insert({chip_val, chip_val});
        }
    }
    log_debug(LogSiliconDriver, "Device IDs and Locations:");
    for (const auto &[chip_id, chip_location] : desc.chip_locations) {
        log_debug(
            LogSiliconDriver,
            "\tchip: {},  EthCoord(x={}, y={}, rack={}, shelf={})",
            chip_id,
            std::get<0>(chip_location),
            std::get<1>(chip_location),
            std::get<2>(chip_location),
            std::get<3>(chip_location));
    }
}

void tt_ClusterDescriptor::load_harvesting_information(YAML::Node &yaml, tt_ClusterDescriptor &desc) {
    if(yaml["harvesting"]) {
        for (const auto& node : yaml["harvesting"].as<std::vector<YAML::Node>>()) {
            const auto& chip_node = node.as<std::map<int, YAML::Node>>();
            chip_id_t chip = chip_node.begin() -> first;
            auto harvesting_info = node.begin() -> second;
            desc.noc_translation_enabled.insert({chip, harvesting_info["noc_translation"].as<bool>()});
            desc.harvesting_masks.insert({chip, harvesting_info["harvest_mask"].as<std::uint32_t>()});
        }
    }
}

void tt_ClusterDescriptor::specify_enabled_devices(const std::vector<chip_id_t> &chip_ids) {
    this->enabled_active_chips.clear();
    for (auto chip_id : chip_ids) {
        this->enabled_active_chips.insert(chip_id);
    }
}

void tt_ClusterDescriptor::enable_all_devices() {
    this->enabled_active_chips = this->all_chips;
}

bool tt_ClusterDescriptor::chips_have_ethernet_connectivity() const { 
    return ethernet_connections.size() > 0; 
}


std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t> > > tt_ClusterDescriptor::get_ethernet_connections() const {
    auto eth_connections = std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t> > >();

    for (const auto &[chip, channel_mapping] : this->ethernet_connections) {
        if (this->enabled_active_chips.find(chip) != this->enabled_active_chips.end()) {
            eth_connections[chip] = {};
            for (const auto &[src_channel, chip_channel] : channel_mapping) {
                const auto &[dest_chip, dest_channel] = chip_channel;
                if (this->enabled_active_chips.find(dest_chip) != this->enabled_active_chips.end()) {
                    eth_connections[chip][src_channel] = chip_channel;
                }
            }
        }
    }
    return eth_connections;
}

std::unordered_map<chip_id_t, eth_coord_t> tt_ClusterDescriptor::get_chip_locations() const {
    static auto locations = std::unordered_map<chip_id_t, eth_coord_t>();
    if (locations.empty()) {
        for (auto chip_id : this->enabled_active_chips) {
            locations[chip_id] = chip_locations.at(chip_id);
        }
    }

    return locations;
}

chip_id_t tt_ClusterDescriptor::get_shelf_local_physical_chip_coords(chip_id_t virtual_coord) {
    // Physical cooridnates of chip inside a single rack. Calculated based on Galaxy topology.
    // See: https://yyz-gitlab.local.tenstorrent.com/tenstorrent/budabackend/-/wikis/uploads/23e7a5168f38dfb706f9887fde78cb03/image.png
    int x = std::get<0>(get_chip_locations().at(virtual_coord));
    int y = std::get<1>(get_chip_locations().at(virtual_coord));
    return 8 * x + y;
}

// Return map, but filter by enabled active chips.
std::unordered_map<chip_id_t, chip_id_t> tt_ClusterDescriptor::get_chips_with_mmio() const {
    auto chips_map = std::unordered_map<chip_id_t, chip_id_t>();
    for (const auto &pair : chips_with_mmio) {
        auto &chip_id = pair.first;
        if (this->enabled_active_chips.find(chip_id) != this->enabled_active_chips.end()) {
            chips_map.insert(pair);
        }
    }

    return chips_map;
}

std::unordered_set<chip_id_t> tt_ClusterDescriptor::get_all_chips() const {
    return this->enabled_active_chips;
}

std::unordered_map<chip_id_t, std::uint32_t> tt_ClusterDescriptor::get_harvesting_info() const {
    return harvesting_masks;
}

std::unordered_map<chip_id_t, bool> tt_ClusterDescriptor::get_noc_translation_table_en() const {
    return noc_translation_enabled;
}

std::size_t tt_ClusterDescriptor::get_number_of_chips() const { return this->enabled_active_chips.size(); }
