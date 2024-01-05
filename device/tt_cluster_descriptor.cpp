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

// given two coordinates, finds the number of hops
// Requirements:
// 1. Two locations must be either galaxy coordinates, or must be on the same shelf&rack on multi-Nebula systems
// 2. Not supported: one coordinate on a nebula, the other on galaxy.
// 3. location_b must be in higher shelf/higher rack than location_a
int tt_ClusterDescriptor::get_ethernet_link_coord_distance(const eth_coord_t &location_a, const eth_coord_t &location_b) {

    // eth_coord_t: x, y, rack, shelf
    int x_distance = std::abs(std::get<0>(location_a) - std::get<0>(location_b));
    int y_distance = std::abs(std::get<1>(location_a) - std::get<1>(location_b));

    int shelf_a = std::get<3>(location_a);
    int shelf_b = std::get<3>(location_b);

    int rack_a = std::get<2>(location_a);
    int rack_b = std::get<2>(location_b);

    // to simplify this function
    log_assert(shelf_a <= shelf_b && rack_a <= rack_b, "location_b is expected to be in higher shelf and rack");

    if(shelf_b > shelf_a) {
        eth_coord_t exit_shelf_a{
            galaxy_shelf_exit_x.at(shelf_a),
            std::get<1>(location_a),
            rack_a,
            shelf_a,
        };

        eth_coord_t entry_shelf_b{
            galaxy_shelf_entry_x.at(shelf_b),
            std::get<1>(location_a),
            rack_a,
            shelf_a + 1,
        };
        // hop onto the next shelf (same y, move along x) and find distance from there
        return get_ethernet_link_coord_distance(location_a, exit_shelf_a) + get_ethernet_link_coord_distance(entry_shelf_b, location_b) + 1;
    }

    if(rack_b > rack_a) {
        eth_coord_t exit_rack_a{
            std::get<0>(location_a),
            galaxy_rack_exit_y.at(rack_a),
            rack_a,
            shelf_a,
        };

        eth_coord_t entry_rack_b{
            std::get<0>(location_a),
            galaxy_rack_entry_y.at(shelf_b),
            rack_a + 1,
            shelf_a,
        };
        // hop onto the next rack (same x, move along y) and find distance from there
        return get_ethernet_link_coord_distance(location_a, exit_rack_a) + get_ethernet_link_coord_distance(entry_rack_b, location_b) + 1;
    }

    // on same shelf/rack, the distance is just x+y difference
    return x_distance + y_distance;
}

// Returns the closest mmio chip to the given chip
// Systems Supported:
// 1. T3000 with 4xN300's, i.e. 2x4 grid of WHs all on one shelf
//    nebula0/chip1 <-> nebula0/chip0/mmio <-> nebula1/chip0/mmio <-> nebula1/chip1
//           |                   |                      |                   |
//    nebula2/chip1 <-> nebula2/chip0/mmio <-> nebula3/chip0/mmio <-> nebula3/chip1
// 2. Other combinations of Nebula's as long as they are on the same shelf
// 3. Nebula(x1 or x2) -> Galaxy -> ... -> Galaxy
// 4. Plink -> Galaxy -> ... -> Galaxy
chip_id_t tt_ClusterDescriptor::get_closest_mmio_capable_chip(const chip_id_t &chip) {
    int min_distance = std::numeric_limits<int>::max();
    chip_id_t closest_chip = chip;
    eth_coord_t chip_eth_coord = this->chip_locations.at(chip);
    int mmio_chip_shelf = -1;
    int mmio_chip_rack = -1;

    std::cout << "get_closest_mmio_capable_chip - chip" << chip << std::endl;

    if(closest_mmio_chip_cache.find(chip) != closest_mmio_chip_cache.end()) {
        std::cout << "\t[cached]closest_mmio_chip to chip" << chip << " is chip" << closest_mmio_chip_cache[chip] << std::endl;
        return closest_mmio_chip_cache[chip];
    }

    for (const auto &pair : this->chips_with_mmio) {
        const chip_id_t &mmio_chip = pair.first;
        eth_coord_t mmio_eth_coord = this->chip_locations.at(mmio_chip);

        std::cout << "\tmmio_eth_coord: ["
            << std::get<0>(mmio_eth_coord) << " "
            << std::get<1>(mmio_eth_coord) << " "
            << std::get<2>(mmio_eth_coord) << " "
            << std::get<3>(mmio_eth_coord) << "]" << std::endl;

        // this function only supports if all mmio chips are on the same shelf and rack
        log_assert(mmio_chip_shelf == -1 || mmio_chip_shelf == std::get<3>(mmio_eth_coord), "mmio chips are on different shelves");
        mmio_chip_shelf = std::get<3>(mmio_eth_coord);
        log_assert(mmio_chip_rack == -1 || mmio_chip_rack == std::get<2>(mmio_eth_coord), "mmio chips are on different racks");
        mmio_chip_rack = std::get<2>(mmio_eth_coord);

        // if the mmio chip and the remote chip are on different shelves or racks,
        // we jump one eth link to handle the case of nebula(shelf0)->galaxy(shelf1)->...->galaxy's
        // in such systems, mmio/nebula can be connected to arbitrary chips on the galaxy, hence, 
        // we cannot simply rely on the coordinates subtraction for the distance
        // but we can rely on the coordinates once we jump to the galaxy chip
        // this assumes that we do not have nebula (shelf0)->nebula (shelf0)->galaxy(shelf1) system, we assert below for that
        if(std::get<2>(mmio_eth_coord) != std::get<2>(chip_eth_coord) || std::get<3>(mmio_eth_coord) != std::get<3>(chip_eth_coord)) {
            for (const auto &[chan, chip_and_chan] : this->ethernet_connections.at(mmio_chip)) {
                const chip_id_t &neighbor_chip = std::get<0>(chip_and_chan);
                eth_coord_t neighbor_eth_coord = this->chip_locations.at(neighbor_chip);

                std::cout << "\t\tneighbor:" << std::get<0>(chip_and_chan) <<
                    " neighbor_eth_coord: ["
                    << std::get<0>(neighbor_eth_coord) << " "
                    << std::get<1>(neighbor_eth_coord) << " "
                    << std::get<2>(neighbor_eth_coord) << " "
                    << std::get<3>(neighbor_eth_coord) << "]" << std::endl;

                // nebula->nebula->galaxy is not supported in this function
                // but nebulax2->galaxy is supported
                // if neighbor is on the shelf, we must have nebulax2, make sure the neighbor of the neighbor is the mmio chip
                if(std::get<3>(mmio_eth_coord) == std::get<3>(neighbor_eth_coord)) {
                    for (const auto &[n2_chan, n2_chip_and_chan] : this->ethernet_connections.at(neighbor_chip)) {
                        const chip_id_t &neighbor2_chip = std::get<0>(n2_chip_and_chan);
                        log_assert(mmio_chip == neighbor2_chip,
                            "On multi-shelf systems mmio chip (nebula) is expected to be connected to another shelf");
                    }
                    continue;
                }
                int distance = get_ethernet_link_coord_distance(neighbor_eth_coord, chip_eth_coord) + 1;
                std::cout << "\t\t\tdistance:" << distance << std::endl;
                if (distance < min_distance) {
                    min_distance = distance;
                    closest_chip = mmio_chip;
                }
            }
        }
        else {
            int distance = get_ethernet_link_coord_distance(mmio_eth_coord, chip_eth_coord);
            if (distance < min_distance) {
                min_distance = distance;
                closest_chip = mmio_chip;
            }
        }
    }
    log_assert(is_chip_mmio_capable(closest_chip), "Closest MMIO chip must be MMIO capable");

    std::cout << "\tclosest_mmio_chip to chip" << chip << " is chip" << closest_chip << " distance:" << min_distance << std::endl;

    closest_mmio_chip_cache[chip] = closest_chip;

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
        desc->coords_to_chip_ids[std::get<2>(chip_location)][std::get<3>(chip_location)][std::get<1>(chip_location)][std::get<0>(chip_location)] = logical_id;
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

    log_debug(LogSiliconDriver, "Chip Coordinates:");
    for (const auto &[rack_id, rack_chip_map] : desc.coords_to_chip_ids) {
        for (const auto &[shelf_id, shelf_chip_map] : rack_chip_map) {
            log_debug(LogSiliconDriver, "\tRack:{} Shelf:{}", rack_id, shelf_id);
            for (const auto &[row, row_chip_map] : shelf_chip_map) {
                std::stringstream row_chips;
                for (const auto &[col, chip_id] : row_chip_map) {
                    row_chips << chip_id << "\t";
                }
                log_debug(LogSiliconDriver, "\t\t{}", row_chips.str());
            }
        }
    }

    // there are 4 ways to connect two galaxy's in shelves (x-dim) or in racks (y-dim)
    // 1. shelf0/x0 <-> shelf1/x0
    // 2. shelf0/x0 <-> shelf1/x3
    // 3. shelf0/x3 <-> shelf1/x0
    // 4. shelf0/x3 <-> shelf1/x3
    // this may be configured differently in different systems,
    // hence instead of hard-coding it, we detect the configuration here based on chip connections

    for (const auto &[chip_id, chip_eth_coord] : desc.chip_locations) {
        // iterate over all neighbors
        for (const auto &[chan, chip_and_chan] : desc.ethernet_connections.at(chip_id)) {
            const chip_id_t &neighbor_chip = std::get<0>(chip_and_chan);
            eth_coord_t neighbor_eth_coord = desc.chip_locations.at(neighbor_chip);
            // shelves in x
            if(std::get<3>(neighbor_eth_coord) != std::get<3>(chip_eth_coord)) {
                eth_coord_t higher_shelf_coord = std::get<3>(neighbor_eth_coord) > std::get<3>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;
                eth_coord_t lower_shelf_coord = std::get<3>(neighbor_eth_coord) < std::get<3>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;

                log_assert(
                    desc.galaxy_shelf_entry_x.find(std::get<3>(higher_shelf_coord)) == desc.galaxy_shelf_entry_x.end() ||
                    desc.galaxy_shelf_entry_x[std::get<3>(higher_shelf_coord)] == std::get<0>(higher_shelf_coord),
                    "unexpected shelf configuration");
                desc.galaxy_shelf_entry_x[std::get<3>(higher_shelf_coord)] = std::get<0>(higher_shelf_coord);

                log_assert(
                    desc.galaxy_shelf_exit_x.find(std::get<3>(lower_shelf_coord)) == desc.galaxy_shelf_exit_x.end() ||
                    desc.galaxy_shelf_exit_x[std::get<3>(lower_shelf_coord)] == std::get<0>(lower_shelf_coord),
                    "unexpected shelf configuration");
                desc.galaxy_shelf_exit_x[std::get<3>(lower_shelf_coord)] = std::get<0>(lower_shelf_coord);
            }

            // racks in y
            if(std::get<2>(neighbor_eth_coord) != std::get<2>(chip_eth_coord)) {
                eth_coord_t higher_rack_coord = std::get<2>(neighbor_eth_coord) > std::get<2>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;
                eth_coord_t lower_rack_coord = std::get<2>(neighbor_eth_coord) < std::get<2>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;

                log_assert(
                    desc.galaxy_rack_entry_y.find(std::get<2>(higher_rack_coord)) == desc.galaxy_rack_entry_y.end() ||
                    desc.galaxy_rack_entry_y[std::get<2>(higher_rack_coord)] == std::get<1>(higher_rack_coord),
                    "unexpected rack configuration");
                desc.galaxy_rack_entry_y[std::get<2>(higher_rack_coord)] = std::get<1>(higher_rack_coord);

                log_assert(
                    desc.galaxy_rack_exit_y.find(std::get<2>(lower_rack_coord)) == desc.galaxy_rack_exit_y.end() ||
                    desc.galaxy_rack_exit_y[std::get<2>(lower_rack_coord)] == std::get<1>(lower_rack_coord),
                    "unexpected rack configuration");
                desc.galaxy_rack_exit_y[std::get<2>(lower_rack_coord)] = std::get<1>(lower_rack_coord);
            }
        }
    }

    for (const auto &[shelf, entry_x] : desc.galaxy_shelf_entry_x) {
        log_debug(LogSiliconDriver, "shelf: {} entry_x: {}", shelf, entry_x);
    }
    for (const auto &[shelf, exit_x] : desc.galaxy_shelf_exit_x) {
        log_debug(LogSiliconDriver, "shelf: {} exit_x: {}", shelf, exit_x);
    }
    for (const auto &[rack, entry_y] : desc.galaxy_rack_entry_y) {
        log_debug(LogSiliconDriver, "rack: {} entry_y: {}", rack, entry_y);
    }
    for (const auto &[rack, exit_y] : desc.galaxy_rack_exit_y) {
        log_debug(LogSiliconDriver, "rack: {} exit_y: {}", rack, exit_y);
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
        desc.coords_to_chip_ids[std::get<2>(chip_location)][std::get<3>(chip_location)][std::get<1>(chip_location)][std::get<0>(chip_location)] = chip_id;
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
