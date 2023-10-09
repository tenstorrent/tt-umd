/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// Assume we are applying the above license globally across the umd?

#pragma once

#include <memory>
#include <unordered_map>

#include "tt_cluster_descriptor_types.h"
#include "tt_xy_pair.h"

class tt_ClusterDescriptor;
class tt_SocDescriptor;

namespace tt {
class RemoteTransferSourceLocator {
    using dest_servicer_lookup_t = std::unordered_map<eth_coord_t, tt_cxy_pair>;

   public:
    /*
     * Will return the ethernet core on the MMIO chip that has the shortest path to the target chip
     *
     * Note: I'm not sure which API makes more sense for BUDA and metal respectively so I wanted to maintain optionality
     *       by keeping both in. We can delete if one is completely redundant
     */
    tt_cxy_pair get_ethernet_core_to_service_request(chip_id_t command_target_chip) const {
        return this->target_chip_servicing_ethernet_core_map.at(this->chip_locations.at(command_target_chip));
    }
    tt_cxy_pair get_ethernet_core_to_service_request(eth_coord_t const& command_target_coords) const {
        return this->target_chip_servicing_ethernet_core_map.at(command_target_coords);
    }

    /*
     * For multi-CPU machines, we want to provide the flexibility to force a remote transfer to be serviced by a
     * specified MMIO device
     *
     * Note: I'm not sure which API makes more sense for BUDA and metal respectively so I wanted to maintain optionality
     *       by keeping both in. We can delete if one is completely redundant
     */
    tt_cxy_pair get_ethernet_core_on_chip_to_service_request(
        chip_id_t command_servicing_chip, chip_id_t command_target_chip) const {
        return this->forced_starting_point_target_chip_servicing_ethernet_core_map
            .at(this->chip_locations.at(command_servicing_chip))
            .at(this->chip_locations.at(command_target_chip));
    }
    tt_cxy_pair get_ethernet_core_on_chip_to_service_request(
        chip_id_t command_servicing_chip, eth_coord_t const& command_target_chip) const {
        return this->forced_starting_point_target_chip_servicing_ethernet_core_map
            .at(this->chip_locations.at(command_servicing_chip))
            .at(command_target_chip);
    }

    static std::unique_ptr<RemoteTransferSourceLocator> create_from_cluster_descriptor(
        tt_SocDescriptor const& soc_descriptor, tt_ClusterDescriptor const& cluster_descriptor);

   protected:
    friend std::unique_ptr<RemoteTransferSourceLocator> std::make_unique<RemoteTransferSourceLocator>(void);

    RemoteTransferSourceLocator(void);

   private:
    // For now, we just clone the datastructure, one per API function since cluster sizes are relatively small (even at
    // thousands of chips)
    dest_servicer_lookup_t target_chip_servicing_ethernet_core_map;

    using mmio_chip_id_t = chip_id_t;
    using distance_t = std::size_t;
    std::unordered_map<mmio_chip_id_t, std::unordered_map<chip_id_t, distance_t>> mmio_chip_to_target_chip_distance_map;
    std::unordered_map<chip_id_t, std::vector<std::pair<mmio_chip_id_t,distance_t>>> target_chip_to_mmio_chip_distance_map;

    //
    std::unordered_map<eth_coord_t, dest_servicer_lookup_t>
        forced_starting_point_target_chip_servicing_ethernet_core_map;

    //
    // For now to let us decouple from cluster_desctriptor (otherwise we need a shared_ptr passed around). We should
    // migrate to
    std::unordered_map<chip_id_t, eth_coord_t> chip_locations;
};

};  // namespace tt