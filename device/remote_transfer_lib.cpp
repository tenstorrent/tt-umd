/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 // Assume we are applying the above license globally across the umd?

#include "remote_transfer_lib.hpp"
#include "device/tt_cluster_descriptor_types.h"
#include "tt_cluster_descriptor.h"
#include "tt_soc_descriptor.h"

#include <cstddef>
#include <optional>
#include <unordered_map>

namespace tt {

ethernet_channel_t compute_servicing_channel(chip_id_t command_servicing_chip, eth_coord_t const& target_chip) {

}


std::unique_ptr<RemoteTransferSourceLocator> RemoteTransferSourceLocator::create_from_cluster_descriptor(tt_SocDescriptor const& soc_descriptor, tt_ClusterDescriptor const& cluster_descriptor) {
    auto locator = std::make_unique<RemoteTransferSourceLocator>();
    using node_to_visit_t = std::pair<chip_id_t, int>; // node, distance

    std::unordered_map<
        chip_id_t,
        std::unordered_map<ethernet_channel_t, std::tuple<chip_id_t, ethernet_channel_t>>> const& cluster_connections =
        cluster_descriptor.get_ethernet_connections();

    ////////------------ helper lambdas ------------ ////////

    auto collect_neighbour_chip_ids = [&cluster_connections, &cluster_descriptor](chip_id_t chip_id, std::vector<chip_id_t> &neighbour_chip_ids) {
        for (auto const& [ethernet_channel, connected_chip_channel] : cluster_connections.at(chip_id)) {
            chip_id_t connected_chip = std::get<0>(connected_chip_channel);
            
            // Typically the connections between the same chip pair are ordered sequentially so for that reason we check the last element
            // in the neighbour list since in the normal case, it'll let us evaluate the condition with a single lookup rather than full traversal
            // of neighbour_chip_ids
            bool already_added_to_neighbour_list = std::find(neighbour_chip_ids.rbegin(), neighbour_chip_ids.rend(), connected_chip) == neighbour_chip_ids.rend();
            if (!already_added_to_neighbour_list) {
                neighbour_chip_ids.push_back(connected_chip);
            }
        }
        return neighbour_chip_ids;
    };

    auto compute_shortest_paths = [&collect_neighbour_chip_ids,&cluster_connections, &cluster_descriptor](chip_id_t mmio_chip, std::unordered_map<chip_id_t, std::size_t> &distance_map) {
        std::vector<node_to_visit_t> traversal_list = {{mmio_chip, 0}};

        std::vector<chip_id_t> neighbours;
        while (traversal_list.size() > 0) {
            auto const& [current_chip_id, distance] = traversal_list.back();

            collect_neighbour_chip_ids(current_chip_id, neighbours);
            for (chip_id_t connected_chip : neighbours) {
                std::size_t connected_distance = distance + 1;
                bool found_shorter_distance = distance_map.find(connected_chip) == distance_map.end() || distance_map.at(connected_chip) > connected_distance;
                
                // Typically have multiple links connected to the same chip so we want to visit them multiple times
                // (waste of compute) - but this check should also skip the that case
                if (found_shorter_distance) {
                    distance_map[connected_chip] = connected_distance;
                    traversal_list.push_back({connected_chip, connected_distance});
                }
            }
            neighbours.clear();
        };
    };


    auto get_eth_channel_connected_to_target_coord = [&cluster_connections,&cluster_descriptor](chip_id_t source_chip, eth_coord_t const& target_coord) {
        for (auto const& [ethernet_channel, connected_chip_channel] : cluster_connections.at(source_chip)) {
            chip_id_t connected_chip = std::get<0>(connected_chip_channel);
            eth_coord_t const& connected_chip_coord = cluster_descriptor.get_chip_coordinates(connected_chip);
            if (connected_chip_coord == target_coord) {
                return std::optional<ethernet_channel_t>(ethernet_channel);
            }
        }
        return std::optional<ethernet_channel_t>(std::nullopt);
    };


    auto get_next_hop_eth_coord_same_shelf_and_rack = [](eth_coord_t const& closest_mmio_chip_coord, eth_coord_t const& target_chip_coord) {
        TT_ASSERT(get_eth_coord_rack(target_chip_coord) == get_eth_coord_rack(closest_mmio_chip_coord) && get_eth_coord_shelf(target_chip_coord) == get_eth_coord_shelf(closest_mmio_chip_coord));
        // if we're on the same rack, we route x first, then y which
        bool right_of_mmio = get_eth_coord_x(target_chip_coord) > get_eth_coord_x(closest_mmio_chip_coord);
        bool above_mmio = get_eth_coord_y(target_chip_coord) > get_eth_coord_y(closest_mmio_chip_coord);
        return eth_coord_t{
            right_of_mmio ? get_eth_coord_x(closest_mmio_chip_coord) + 1 : get_eth_coord_x(closest_mmio_chip_coord) - 1,
            above_mmio ? get_eth_coord_y(closest_mmio_chip_coord) + 1 : get_eth_coord_y(closest_mmio_chip_coord) - 1,
            get_eth_coord_rack(closest_mmio_chip_coord),
            get_eth_coord_shelf(closest_mmio_chip_coord)
        };
    };

    auto get_next_hop_eth_coord_different_shelf_or_rack = [&compute_shortest_paths,&collect_neighbour_chip_ids,&cluster_descriptor](chip_id_t closest_mmio_chip, chip_id_t target_chip) {
        // we just pick any core connected on a link that is closest to the target chip
        std::vector<chip_id_t> mmio_neighbours;
        collect_neighbour_chip_ids(closest_mmio_chip, mmio_neighbours);
        int closest_neighbour_index = -1;
        std::size_t closest_neighbour_distance = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < mmio_neighbours.size(); ++i) {
            chip_id_t neighbour = mmio_neighbours[i];
            eth_coord_t const& neighbour_coord = cluster_descriptor.get_chip_coordinates(neighbour);
            std::unordered_map<chip_id_t, distance_t> distance_map = {};
            compute_shortest_paths(neighbour, distance_map);

            if (distance_map.at(target_chip) < closest_neighbour_distance) {
                closest_neighbour_distance = distance_map.at(target_chip);
                closest_neighbour_index = neighbour;
            }
        }
        TT_ASSERT(closest_neighbour_index != -1, "Couldn't find any neighbours that have route to target chip {}", target_chip);

        return cluster_descriptor.get_chip_coordinates(mmio_neighbours[closest_neighbour_index]); 
    };

    ////////------------ main implementation ------------ ////////

    // for every host connection, do shortest distance traversal
    for (chip_id_t mmio_chip : cluster_descriptor.get_chips_with_mmio()) {
        auto distance_from_mmio_chip = std::unordered_map<eth_coord_t, std::size_t>{{mmio_chip, 0}};
        compute_shortest_paths(mmio_chip, locator->mmio_chip_to_target_chip_distance_map[mmio_chip]);
    }

    for (chip_id_t chip_id : cluster_descriptor.get_all_chips()) {

        locator->chip_locations.insert({chip_id, cluster_descriptor.get_chip_coordinates(chip_id)});

        for (auto const& [mmio_chip, target_distance_map] : locator->mmio_chip_to_target_chip_distance_map) {
            if (target_distance_map.find(chip_id) != target_distance_map.end()) {
                locator->target_chip_to_mmio_chip_distance_map[chip_id].push_back({mmio_chip, target_distance_map.at(chip_id)});
            }
        }

        // sort in ascending order for convenience
        std::sort(locator->target_chip_to_mmio_chip_distance_map.at(chip_id).begin(), locator->target_chip_to_mmio_chip_distance_map.at(chip_id).end(), [](auto const& a, auto const& b) {
            return a.second < b.second;
        });
    }


    for (auto const& [target_chip, mmio_chip_distances] : locator->target_chip_to_mmio_chip_distance_map) {
        for (auto iter = mmio_chip_distances.begin(); iter != mmio_chip_distances.end(); ++iter) {            
            auto const& [mmio_chip, mmio_chip_distance] = mmio_chip_distances.front();
            eth_coord_t const& target_chip_coord = cluster_descriptor.get_chip_coordinates(target_chip);
            eth_coord_t const& mmio_chip_coord = cluster_descriptor.get_chip_coordinates(mmio_chip);
            bool on_same_rack_and_shelf = get_eth_coord_rack(target_chip_coord) == get_eth_coord_rack(mmio_chip_coord) && get_eth_coord_shelf(target_chip_coord) == get_eth_coord_shelf(mmio_chip_coord);

            eth_coord_t const& next_hop_eth_coord = (on_same_rack_and_shelf) ? 
                get_next_hop_eth_coord_same_shelf_and_rack(mmio_chip_coord, target_chip_coord) :
                get_next_hop_eth_coord_different_shelf_or_rack(mmio_chip, target_chip_coord);

            std::optional<ethernet_channel_t> servicing_ethernet_channel = get_eth_channel_connected_to_target_coord(mmio_chip, next_hop_eth_coord);

            TT_ASSERT(servicing_ethernet_channel.has_value(), "Could not find expected next hop for chip {} to chip {}", mmio_chip, target_chip);
            tt_xy_pair eth_core_xy = soc_descriptor.ethernet_cores.at(servicing_ethernet_channel.value());

            // reuse the result for the first entry here and the next loop when we compute this for the further mmio chips,
            // we'll skip the first entry since we already computed it (here)
            locator->forced_starting_point_target_chip_servicing_ethernet_core_map[mmio_chip].insert({target_chip_coord, eth_core_xy});
        
            bool is_closest_mmio_chip = iter == mmio_chip_distances.begin();
            if (is_closest_mmio_chip) {
                locator->target_chip_servicing_ethernet_core_map.insert({target_chip_coord, {mmio_chip, eth_core_xy}});
            }
        }
    }


    return locator;
}


}; // namespace tt