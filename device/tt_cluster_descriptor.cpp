// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0


#include "tt_cluster_descriptor.h"
#include "libs/create_ethernet_map.h"

#include <fstream>
#include <memory>
#include <sstream> 

#include "common/logger.hpp"
#include "yaml-cpp/yaml.h"

#include "fmt/core.h"

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

bool tt_ClusterDescriptor::is_chip_mmio_capable(const chip_id_t chip_id) const {
    return this->chips_with_mmio.find(chip_id) != this->chips_with_mmio.end();
}

bool tt_ClusterDescriptor::is_chip_remote(const chip_id_t chip_id) const {
    return !is_chip_mmio_capable(chip_id);
}

// given two coordinates, finds the number of hops between the two chips
// it assumes that shelves are connected in x-dim and racks are connected in y-dim
// it recursively hops between shelves (in x-dim) until the correct shelf is found,
// then it recursively hops between racks (in y-dim) until the correct rack is found,
// then once a chip on the same shelf&rack is found,
// the distance from this chip to either location_a or location_b is just x&y dim difference.
// the function returns the total distance of travelled between shelves and racks, plust the x&y dim difference
int tt_ClusterDescriptor::get_ethernet_link_coord_distance(const eth_coord_t &location_a, const eth_coord_t &location_b) const {

    log_trace(LogSiliconDriver, "get_ethernet_link_coord_distance from ({}, {}, {}, {}) to ({}, {}, {}, {})",
        std::get<0>(location_a), std::get<1>(location_a), std::get<2>(location_a), std::get<3>(location_a),
        std::get<0>(location_b), std::get<1>(location_b), std::get<2>(location_b), std::get<3>(location_b));

    // eth_coord_t: x, y, rack, shelf

    int x_a = std::get<0>(location_a);
    int x_b = std::get<0>(location_b);

    int y_a = std::get<1>(location_a);
    int y_b = std::get<1>(location_b);

    int shelf_a = std::get<3>(location_a);
    int shelf_b = std::get<3>(location_b);

    int rack_a = std::get<2>(location_a);
    int rack_b = std::get<2>(location_b);

    int x_distance = std::abs(x_a - x_b);
    int y_distance = std::abs(y_a - y_b);

    // move along y-dim to exit from the shelf to go to a higher shelf
    if(shelf_b > shelf_a) {
        // this is already verified where galaxy_shelves_exit_chip_coords_per_y_dim is populated, but just to be safe
        log_assert(galaxy_shelves_exit_chip_coords_per_y_dim.find(shelf_a) != galaxy_shelves_exit_chip_coords_per_y_dim.end(),
            "Expected shelf-to-shelf connection");
        // this row does not have a shelf-to-shelf connection
        if(galaxy_shelves_exit_chip_coords_per_y_dim.at(shelf_a).find(y_a) == galaxy_shelves_exit_chip_coords_per_y_dim.at(shelf_a).end()) {
            return std::numeric_limits<int>::max();
        }

        const Chip2ChipConnection& shelf_to_shelf_connection = galaxy_shelves_exit_chip_coords_per_y_dim.at(shelf_a).at(y_a);
        log_assert(shelf_to_shelf_connection.destination_chip_coords.size(), "Expecting at least one shelf-to-shelf connection, possibly one-to-many");

        // for each shelf-to-shelf connection at y_a, find the distance to location_b, take min
        int distance = std::numeric_limits<int>::max();
        eth_coord_t exit_shelf = shelf_to_shelf_connection.source_chip_coord;
        for(eth_coord_t next_shelf : shelf_to_shelf_connection.destination_chip_coords) {

            log_assert(std::get<1>(exit_shelf) == y_a && std::get<3>(exit_shelf) == shelf_a && std::get<2>(exit_shelf) == rack_a,
                "Invalid shelf exit coordinates");

            // next shelf could be at a different y-dim in nebula->galaxy systems
            log_assert(std::get<3>(next_shelf) == (shelf_a+1) && std::get<2>(next_shelf) == rack_a,
                "Invalid shelf entry coordinates");

            // hop onto the next shelf and find distance from there
            int distance_to_exit = get_ethernet_link_coord_distance(location_a, exit_shelf);
            int distance_in_next_shelf = get_ethernet_link_coord_distance(next_shelf, location_b);
            // no path found
            if(distance_to_exit == std::numeric_limits<int>::max() || distance_in_next_shelf == std::numeric_limits<int>::max()) {
                continue;
            }
            distance = std::min(distance, distance_to_exit + distance_in_next_shelf + 1);
        }
        log_trace(LogSiliconDriver, "\tdistance from ({}, {}, {}, {}) to ({}, {}, {}, {}) is {}",
            std::get<0>(location_a), std::get<1>(location_a), std::get<2>(location_a), std::get<3>(location_a),
            std::get<0>(location_b), std::get<1>(location_b), std::get<2>(location_b), std::get<3>(location_b), distance);
        return distance;
    }
    else if(shelf_a > shelf_b) {

        // this is already verified where galaxy_shelves_exit_chip_coords_per_y_dim is populated, but just to be safe
        log_assert(galaxy_shelves_exit_chip_coords_per_y_dim.find(shelf_b) != galaxy_shelves_exit_chip_coords_per_y_dim.end(),
            "Expected shelf-to-shelf connection");
        // this row does not have a shelf-to-shelf connection
        if(galaxy_shelves_exit_chip_coords_per_y_dim.at(shelf_b).find(y_b) == galaxy_shelves_exit_chip_coords_per_y_dim.at(shelf_b).end()) {
            return std::numeric_limits<int>::max();
        }

        const Chip2ChipConnection& shelf_to_shelf_connection = galaxy_shelves_exit_chip_coords_per_y_dim.at(shelf_b).at(y_b);
        log_assert(shelf_to_shelf_connection.destination_chip_coords.size(), "Expecting at least one shelf-to-shelf connection, possibly one-to-many")

        // for each shelf-to-shelf connection at y_b, find the distance to location_a, take min
        int distance = std::numeric_limits<int>::max();
        eth_coord_t exit_shelf = shelf_to_shelf_connection.source_chip_coord;
        for(eth_coord_t next_shelf : shelf_to_shelf_connection.destination_chip_coords) {

            log_assert(std::get<1>(exit_shelf) == y_b && std::get<3>(exit_shelf) == shelf_b && std::get<2>(exit_shelf) == rack_b,
                "Invalid shelf exit coordinates");
            // next shelf could be at a different y-dim in nebula->galaxy systems
            log_assert(std::get<3>(next_shelf) == (shelf_b+1) && std::get<2>(next_shelf) == rack_b,
                "Invalid shelf entry coordinates");

            // hop onto the next shelf and find distance from there
            int distance_to_exit = get_ethernet_link_coord_distance(location_b, exit_shelf);
            int distance_in_next_shelf = get_ethernet_link_coord_distance(next_shelf, location_a);
            // no path found
            if(distance_to_exit == std::numeric_limits<int>::max() || distance_in_next_shelf == std::numeric_limits<int>::max()) {
                continue;
            }
            distance = std::min(distance, distance_to_exit + distance_in_next_shelf + 1);
        }
        log_trace(LogSiliconDriver, "\tdistance from ({}, {}, {}, {}) to ({}, {}, {}, {}) is {}",
            std::get<0>(location_a), std::get<1>(location_a), std::get<2>(location_a), std::get<3>(location_a),
            std::get<0>(location_b), std::get<1>(location_b), std::get<2>(location_b), std::get<3>(location_b), distance);
        return distance;
    }

    // move along y-dim to exit from the shelf to go to a higher shelf
    if(rack_b > rack_a) {

        // this is already verified where galaxy_racks_exit_chip_coords_per_x_dim is populated, but just to be safe
        log_assert(galaxy_racks_exit_chip_coords_per_x_dim.find(rack_a) != galaxy_racks_exit_chip_coords_per_x_dim.end(),
            "Expected rack-to-rack connection");

        // this row does not have a rack-to-rack connection
        if(galaxy_racks_exit_chip_coords_per_x_dim.at(rack_a).find(x_a) == galaxy_racks_exit_chip_coords_per_x_dim.at(rack_a).end()) {
            return std::numeric_limits<int>::max();
        }

        const Chip2ChipConnection& rack_to_rack_connection = galaxy_racks_exit_chip_coords_per_x_dim.at(rack_a).at(x_a);
        log_assert(rack_to_rack_connection.destination_chip_coords.size(), "Expecting at least one rack-to-rack connection, possibly one-to-many");

        // for each rack-to-rack connection at x_a, find the distance to location_b, take min
        int distance = std::numeric_limits<int>::max();
        eth_coord_t exit_rack = rack_to_rack_connection.source_chip_coord;
        for(eth_coord_t next_rack : rack_to_rack_connection.destination_chip_coords) {

            log_assert(std::get<0>(exit_rack) == x_a && std::get<3>(exit_rack) == shelf_a && std::get<2>(exit_rack) == rack_a,
                "Invalid rack exit coordinates");
            log_assert(std::get<0>(next_rack) == x_a && std::get<3>(next_rack) == shelf_a && std::get<2>(next_rack) == (rack_a+1),
                "Invalid rack entry coordinates");

            // hop onto the next rack and find distance from there
            int distance_to_exit = get_ethernet_link_coord_distance(location_a, exit_rack);
            int distance_in_next_rack = get_ethernet_link_coord_distance(next_rack, location_b);
            // no path found
            if (distance_to_exit == std::numeric_limits<int>::max() || distance_in_next_rack == std::numeric_limits<int>::max()) {
                continue;
            }
            distance = std::min(distance, distance_to_exit + distance_in_next_rack + 1);
        }
        log_trace(LogSiliconDriver, "\tdistance from ({}, {}, {}, {}) to ({}, {}, {}, {}) is {}",
            std::get<0>(location_a), std::get<1>(location_a), std::get<2>(location_a), std::get<3>(location_a),
            std::get<0>(location_b), std::get<1>(location_b), std::get<2>(location_b), std::get<3>(location_b), distance);

        return distance;
    }
    else if(rack_a > rack_b) {

        // this is already verified where galaxy_racks_exit_chip_coords_per_x_dim is populated, but just to be safe
        log_assert(galaxy_racks_exit_chip_coords_per_x_dim.find(rack_b) != galaxy_racks_exit_chip_coords_per_x_dim.end(),
            "Expected rack-to-rack connection");

        // this row does not have a rack-to-rack connection
        if(galaxy_racks_exit_chip_coords_per_x_dim.at(rack_b).find(x_b) == galaxy_racks_exit_chip_coords_per_x_dim.at(rack_b).end()) {
            return std::numeric_limits<int>::max();
        }

        const Chip2ChipConnection& rack_to_rack_connection = galaxy_racks_exit_chip_coords_per_x_dim.at(rack_b).at(x_b);
        log_assert(rack_to_rack_connection.destination_chip_coords.size(), "Expecting at least one rack-to-rack connection, possibly one-to-many");

        // for each rack-to-rack connection at x_a, find the distance to location_b, take min
        int distance = std::numeric_limits<int>::max();
        eth_coord_t exit_rack = rack_to_rack_connection.source_chip_coord;
        for(eth_coord_t next_rack : rack_to_rack_connection.destination_chip_coords) {

            log_assert(std::get<0>(exit_rack) == x_b && std::get<3>(exit_rack) == shelf_b && std::get<2>(exit_rack) == rack_b,
                "Invalid rack exit coordinates");
            log_assert(std::get<0>(next_rack) == x_b && std::get<3>(next_rack) == shelf_b && std::get<2>(next_rack) == (rack_b+1),
                "Invalid rack entry coordinates");

            // hop onto the next rack and find distance from there
            int distance_to_exit = get_ethernet_link_coord_distance(location_b, exit_rack);
            int distance_in_next_rack = get_ethernet_link_coord_distance(next_rack, location_a);
            // no path found
            if (distance_to_exit == std::numeric_limits<int>::max() || distance_in_next_rack == std::numeric_limits<int>::max()) {
                continue;
            }
            distance = std::min(distance, distance_to_exit + distance_in_next_rack + 1);
        }
        log_trace(LogSiliconDriver, "\tdistance from ({}, {}, {}, {}) to ({}, {}, {}, {}) is {}",
            std::get<0>(location_a), std::get<1>(location_a), std::get<2>(location_a), std::get<3>(location_a),
            std::get<0>(location_b), std::get<1>(location_b), std::get<2>(location_b), std::get<3>(location_b), distance);

        return distance;
    }

    log_trace(LogSiliconDriver, "\tdistance from ({}, {}, {}, {}) to ({}, {}, {}, {}) is {}",
        std::get<0>(location_a), std::get<1>(location_a), std::get<2>(location_a), std::get<3>(location_a),
        std::get<0>(location_b), std::get<1>(location_b), std::get<2>(location_b), std::get<3>(location_b), x_distance + y_distance);

    // on same shelf/rack, the distance is just x+y difference
    return x_distance + y_distance;
}

// Returns the closest mmio chip to the given chip
chip_id_t tt_ClusterDescriptor::get_closest_mmio_capable_chip(const chip_id_t chip) {

    log_debug(LogSiliconDriver, "get_closest_mmio_chip to chip{}", chip);

    if (this->is_chip_mmio_capable(chip)) {
        return chip;
    }

    if(closest_mmio_chip_cache.find(chip) != closest_mmio_chip_cache.end()) {
        return closest_mmio_chip_cache[chip];
    }

    int min_distance = std::numeric_limits<int>::max();
    chip_id_t closest_chip = chip;
    eth_coord_t chip_eth_coord = this->chip_locations.at(chip);

    for (const auto &pair : this->chips_with_mmio) {
        const chip_id_t &mmio_chip = pair.first;
        eth_coord_t mmio_eth_coord = this->chip_locations.at(mmio_chip);

        log_debug(LogSiliconDriver, "Checking chip{} at ({}, {}, {}, {})", mmio_chip, std::get<0>(mmio_eth_coord), std::get<1>(mmio_eth_coord), std::get<2>(mmio_eth_coord), std::get<3>(mmio_eth_coord));

        int distance = get_ethernet_link_coord_distance(mmio_eth_coord, chip_eth_coord);
        if (distance < min_distance) {
            min_distance = distance;
            closest_chip = mmio_chip;
        }
    }
    log_assert(min_distance != std::numeric_limits<int>::max(), "Chip{} is not connected to any MMIO capable chip", chip);

    log_assert(is_chip_mmio_capable(closest_chip), "Closest MMIO chip must be MMIO capable");

    log_debug(LogSiliconDriver, "closest_mmio_chip to chip{} is chip{} distance:{}", chip, closest_chip, min_distance);

    closest_mmio_chip_cache[chip] = closest_chip;

    return closest_chip;
}

std::string tt_ClusterDescriptor::get_cluster_descriptor_file_path() {
    static std::string yaml_path;
    static bool is_initialized = false;
    if (!is_initialized){
        // Cluster descriptor path will be created in the working directory.        
        std::filesystem::path cluster_path = std::filesystem::path("cluster_descriptor.yaml");
        if (!std::filesystem::exists(cluster_path)){
            auto val = system ( ("touch " + cluster_path.string()).c_str());
            if(val != 0) throw std::runtime_error("Cluster Generation Failed!");
        }

        int val = create_ethernet_map((char*)cluster_path.string().c_str());
        if(val != 0) throw std::runtime_error("Cluster Generation Failed!");
        yaml_path = cluster_path.string();
        is_initialized = true;
    }
    return yaml_path;
}

std::unique_ptr<tt_ClusterDescriptor> tt_ClusterDescriptor::create_from_yaml(const std::string &cluster_descriptor_file_path) {
    std::unique_ptr<tt_ClusterDescriptor> desc = std::unique_ptr<tt_ClusterDescriptor>(new tt_ClusterDescriptor());

    std::ifstream fdesc(cluster_descriptor_file_path);
    if (fdesc.fail()) {
        throw std::runtime_error(fmt::format("Error: cluster connectivity descriptor file {} does not exist!", cluster_descriptor_file_path));
    }
    fdesc.close();

    YAML::Node yaml = YAML::LoadFile(cluster_descriptor_file_path);
    tt_ClusterDescriptor::load_chips_from_connectivity_descriptor(yaml, *desc);
    tt_ClusterDescriptor::load_ethernet_connections_from_connectivity_descriptor(yaml, *desc);
    tt_ClusterDescriptor::load_harvesting_information(yaml, *desc);
    desc->enable_all_devices();

    desc->fill_chips_grouped_by_closest_mmio();

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

    int highest_shelf_id = 0;
    int highest_rack_id = 0;

    // shelves and racks can be connected at different chip coordinates
    // determine which chips are connected to the next (i.e. higher id) shelf/rack and what the coordinate of the chip on the other shelf/rack is
    // this is used in get_ethernet_link_coord_distance to find the distance between two chips
    for (const auto &[chip_id, chip_eth_coord] : desc.chip_locations) {
        highest_shelf_id = std::max(highest_shelf_id, std::get<3>(chip_eth_coord));
        highest_rack_id = std::max(highest_rack_id, std::get<2>(chip_eth_coord));
        // iterate over all neighbors
        if(desc.ethernet_connections.find(chip_id) == desc.ethernet_connections.end()) {
            continue; // chip has no eth connections
        }
        for (const auto &[chan, chip_and_chan] : desc.ethernet_connections.at(chip_id)) {
            const chip_id_t &neighbor_chip = std::get<0>(chip_and_chan);
            eth_coord_t neighbor_eth_coord = desc.chip_locations.at(neighbor_chip);
            // shelves are connected in x-dim
            if(std::get<3>(neighbor_eth_coord) != std::get<3>(chip_eth_coord)) {
                eth_coord_t higher_shelf_coord = std::get<3>(neighbor_eth_coord) > std::get<3>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;
                eth_coord_t lower_shelf_coord = std::get<3>(neighbor_eth_coord) < std::get<3>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;
                int lower_shelf_id = std::get<3>(lower_shelf_coord);
                int lower_shelf_y = std::get<1>(lower_shelf_coord);

                auto& galaxy_shelf_exit_chip_coords_per_y_dim = desc.galaxy_shelves_exit_chip_coords_per_y_dim[lower_shelf_id];

                log_assert(
                    galaxy_shelf_exit_chip_coords_per_y_dim.find(lower_shelf_y) == galaxy_shelf_exit_chip_coords_per_y_dim.end() ||
                    galaxy_shelf_exit_chip_coords_per_y_dim[lower_shelf_y].source_chip_coord == lower_shelf_coord,
                    "Expected a single exit chip on each shelf row");
                galaxy_shelf_exit_chip_coords_per_y_dim[lower_shelf_y].source_chip_coord = lower_shelf_coord;
                galaxy_shelf_exit_chip_coords_per_y_dim[lower_shelf_y].destination_chip_coords.insert(higher_shelf_coord);
            }

            // racks are connected in y-dim
            if(std::get<2>(neighbor_eth_coord) != std::get<2>(chip_eth_coord)) {
                eth_coord_t higher_rack_coord = std::get<2>(neighbor_eth_coord) > std::get<2>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;
                eth_coord_t lower_rack_coord = std::get<2>(neighbor_eth_coord) < std::get<2>(chip_eth_coord) ? neighbor_eth_coord : chip_eth_coord;
                int lower_rack_id = std::get<2>(lower_rack_coord);
                int lower_rack_x = std::get<0>(lower_rack_coord);

                auto& galaxy_rack_exit_chip_coords_per_x_dim = desc.galaxy_racks_exit_chip_coords_per_x_dim[lower_rack_id];

                log_assert(
                    galaxy_rack_exit_chip_coords_per_x_dim.find(lower_rack_x) == galaxy_rack_exit_chip_coords_per_x_dim.end() ||
                    galaxy_rack_exit_chip_coords_per_x_dim[lower_rack_x].source_chip_coord == lower_rack_coord,
                    "Expected a single exit chip on each rack column");
                galaxy_rack_exit_chip_coords_per_x_dim[lower_rack_x].source_chip_coord = lower_rack_coord;
                galaxy_rack_exit_chip_coords_per_x_dim[lower_rack_x].destination_chip_coords.insert(higher_rack_coord);
            }
        }
    }

    // verify that every shelf (except the highest in id) is found in galaxy_shelves_exit_chip_coords_per_y_dim
    // this means that we expect the shelves to be connected linearly in a daisy-chain fashion.
    // shelf0->shelf1->shelf2->...->shelfN
    for(int shelf_id = 0; shelf_id < highest_shelf_id; shelf_id++) {
        log_assert(desc.galaxy_shelves_exit_chip_coords_per_y_dim.find(shelf_id) != desc.galaxy_shelves_exit_chip_coords_per_y_dim.end(),
            "Expected shelf {} to be connected to the next shelf", shelf_id);
    }

    // this prints the exit chip coordinates for each shelf
    // this is used in get_ethernet_link_coord_distance to find the distance between two chips
    for (const auto &[shelf, shelf_exit_chip_coords_per_y_dim] : desc.galaxy_shelves_exit_chip_coords_per_y_dim) {
        for (const auto &[y_dim, shelf_exit_chip_coords] : shelf_exit_chip_coords_per_y_dim) {
            log_debug(LogSiliconDriver, "shelf: {} y_dim: {} exit_coord:({}, {}, {}, {})",
                shelf, y_dim,
                std::get<0>(shelf_exit_chip_coords.source_chip_coord), std::get<1>(shelf_exit_chip_coords.source_chip_coord),
                std::get<2>(shelf_exit_chip_coords.source_chip_coord), std::get<3>(shelf_exit_chip_coords.source_chip_coord));
            for (const auto &destination_chip_coord : shelf_exit_chip_coords.destination_chip_coords) {
                // print shelf_exit_chip_coord in the format: (x, y, rack, shelf)
                log_debug(LogSiliconDriver, "\tdestination_chip_coord: ({}, {}, {}, {})",
                    std::get<0>(destination_chip_coord), std::get<1>(destination_chip_coord), std::get<2>(destination_chip_coord), std::get<3>(destination_chip_coord));
            }
        }
    }

    // verify that every rack (except the highest in id) is found in galaxy_racks_exit_chip_coords_per_x_dim
    // this means that we expect the racks to be connected linearly in a daisy-chain fashion.
    // rack0->rack1->rack2->...->rackN
    for(int rack_id = 0; rack_id < highest_rack_id; rack_id++) {
        log_assert(desc.galaxy_racks_exit_chip_coords_per_x_dim.find(rack_id) != desc.galaxy_racks_exit_chip_coords_per_x_dim.end(),
            "Expected rack {} to be connected to the next rack", rack_id);
    }

    // this prints the exit chip coordinates for each rack
    // this is used in get_ethernet_link_coord_distance to find the distance between two chips
    for (const auto &[rack, rack_exit_chip_coords_per_x_dim] : desc.galaxy_racks_exit_chip_coords_per_x_dim) {
        for (const auto &[x_dim, rack_exit_chip_coords] : rack_exit_chip_coords_per_x_dim) {
            log_debug(LogSiliconDriver, "rack: {} x_dim: {} exit_coord:({}, {}, {}, {})", rack, x_dim,
                std::get<0>(rack_exit_chip_coords.source_chip_coord), std::get<1>(rack_exit_chip_coords.source_chip_coord),
                std::get<2>(rack_exit_chip_coords.source_chip_coord), std::get<3>(rack_exit_chip_coords.source_chip_coord));
            for (const auto &destination_chip_coord : rack_exit_chip_coords.destination_chip_coords) {
                log_debug(LogSiliconDriver, "\tdestination_chip_coord: ({}, {}, {}, {})",
                    std::get<0>(destination_chip_coord), std::get<1>(destination_chip_coord), std::get<2>(destination_chip_coord), std::get<3>(destination_chip_coord));
            }
        }
    }
}

void tt_ClusterDescriptor::load_chips_from_connectivity_descriptor(YAML::Node &yaml, tt_ClusterDescriptor &desc) {

    for (YAML::const_iterator node = yaml["arch"].begin(); node != yaml["arch"].end(); ++node) {
        chip_id_t chip_id = node->first.as<int>();
        desc.all_chips.insert(chip_id);
    }

    for (YAML::const_iterator node = yaml["chips"].begin(); node != yaml["chips"].end(); ++node) {
        chip_id_t chip_id = node->first.as<int>();
        std::vector<int> chip_rack_coords = node->second.as<std::vector<int>>();
        log_assert(chip_rack_coords.size() == 4, "Galaxy (x, y, rack, shelf) coords must be size 4");
        eth_coord_t chip_location{
            chip_rack_coords.at(0), chip_rack_coords.at(1), chip_rack_coords.at(2), chip_rack_coords.at(3)};

        desc.chip_locations.insert({chip_id, chip_location});
        desc.coords_to_chip_ids[std::get<2>(chip_location)][std::get<3>(chip_location)][std::get<1>(chip_location)][std::get<0>(chip_location)] = chip_id;
    }
    
    for(const auto& chip : yaml["chips_with_mmio"]) {
        if(chip.IsMap()) {
            const auto &chip_map = chip.as<std::map<chip_id_t, chip_id_t>>();
            const auto &chips = chip_map.begin();
            desc.chips_with_mmio.insert({chips->first, chips->second});
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

	if (yaml["boardtype"]) {
        for (const auto& chip_board_type : yaml["boardtype"].as<std::map<int, std::string>>()) {
            auto &chip = chip_board_type.first;
            BoardType board_type;
            if (chip_board_type.second == "n150") {
                board_type = BoardType::N150;
            } else if (chip_board_type.second == "n300") {
                board_type = BoardType::N300;
            } else if (chip_board_type.second == "GALAXY") {
                board_type = BoardType::GALAXY;
            } else if (chip_board_type.second == "e150") {
                board_type = BoardType::E150;
            }
            else if (chip_board_type.second == "p150A") {
                board_type = BoardType::P150A;
            } else {
                log_warning(LogSiliconDriver, "Unknown board type for chip {}. This might happen because chip is running old firmware. Defaulting to DEFAULT", chip);
                board_type = BoardType::DEFAULT;
            }
            desc.chip_board_type.insert({chip, board_type});
        }
    } else {
        for (const auto& chip: desc.all_chips) {
            desc.chip_board_type.insert({chip, BoardType::DEFAULT});
        }
    }
}

void tt_ClusterDescriptor::load_harvesting_information(YAML::Node &yaml, tt_ClusterDescriptor &desc) {
    if(yaml["harvesting"]) {
        for (const auto& chip_node : yaml["harvesting"].as<std::map<int, YAML::Node>>()) {
            chip_id_t chip = chip_node.first;
            auto harvesting_info = chip_node.second;
            desc.noc_translation_enabled.insert({chip, harvesting_info["noc_translation"].as<bool>()});
            desc.harvesting_masks.insert({chip, harvesting_info["harvest_mask"].as<std::uint32_t>()});
        }
    }
}

void tt_ClusterDescriptor::enable_all_devices() {
    this->enabled_active_chips = this->all_chips;
}

void tt_ClusterDescriptor::fill_chips_grouped_by_closest_mmio() {
    for (const auto &chip : this->all_chips) {
        // This will also fill up the closest_mmio_chip_cache
        chip_id_t closest_mmio_chip = get_closest_mmio_capable_chip(chip);
        this->chips_grouped_by_closest_mmio[closest_mmio_chip].insert(chip);
    }
}

const std::unordered_map<chip_id_t, std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t> > > tt_ClusterDescriptor::get_ethernet_connections() const {
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

const std::unordered_map<chip_id_t, eth_coord_t>& tt_ClusterDescriptor::get_chip_locations() const {
    static auto locations = std::unordered_map<chip_id_t, eth_coord_t>();
    if (locations.empty() and !this->chip_locations.empty()) {
        for (auto chip_id : this->enabled_active_chips) {
            locations[chip_id] = chip_locations.at(chip_id);
        }
    }

    return locations;
}

chip_id_t tt_ClusterDescriptor::get_shelf_local_physical_chip_coords(chip_id_t virtual_coord) {
    log_assert(!this->chip_locations.empty(), "Getting physical chip coordinates is only valid for systems where chips have coordinates");
    // Physical cooridnates of chip inside a single rack. Calculated based on Galaxy topology.
    // See: https://yyz-gitlab.local.tenstorrent.com/tenstorrent/budabackend/-/wikis/uploads/23e7a5168f38dfb706f9887fde78cb03/image.png
    int x = std::get<0>(get_chip_locations().at(virtual_coord));
    int y = std::get<1>(get_chip_locations().at(virtual_coord));
    return 8 * x + y;
}

// Return map, but filter by enabled active chips.
const std::unordered_map<chip_id_t, chip_id_t> tt_ClusterDescriptor::get_chips_with_mmio() const {
    auto chips_map = std::unordered_map<chip_id_t, chip_id_t>();
    for (const auto &pair : chips_with_mmio) {
        auto &chip_id = pair.first;
        if (this->enabled_active_chips.find(chip_id) != this->enabled_active_chips.end()) {
            chips_map.insert(pair);
        }
    }

    return chips_map;
}

const std::unordered_set<chip_id_t>& tt_ClusterDescriptor::get_all_chips() const {
    return this->enabled_active_chips;
}

const std::unordered_map<chip_id_t, std::uint32_t>& tt_ClusterDescriptor::get_harvesting_info() const {
    return harvesting_masks;
}

const std::unordered_map<chip_id_t, bool>& tt_ClusterDescriptor::get_noc_translation_table_en() const {
    return noc_translation_enabled;
}

std::size_t tt_ClusterDescriptor::get_number_of_chips() const { return this->enabled_active_chips.size(); }

int tt_ClusterDescriptor::get_ethernet_link_distance(chip_id_t chip_a, chip_id_t chip_b) const {
    log_assert(!this->chip_locations.empty(), "Getting physical chip coordinates is only valid for systems where chips have coordinates");
    return this->get_ethernet_link_coord_distance(chip_locations.at(chip_a), chip_locations.at(chip_b));
}

BoardType tt_ClusterDescriptor::get_board_type(chip_id_t chip_id) const {
  BoardType board_type = this->chip_board_type.at(chip_id);
  return board_type;
}

const std::unordered_map<chip_id_t, std::unordered_set<chip_id_t>>& tt_ClusterDescriptor::get_chips_grouped_by_closest_mmio() const {
    return chips_grouped_by_closest_mmio;
}
