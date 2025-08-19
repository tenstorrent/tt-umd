/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery_blackhole.h"

#include <tt-logger/tt-logger.hpp>

#include "api/umd/device/topology/topology_discovery_blackhole.h"
#include "umd/device/arch/blackhole_implementation.h"
#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/remote_chip.h"
#include "umd/device/remote_communication.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/types/blackhole_eth.h"
#include "umd/device/types/cluster_types.h"

extern bool umd_use_noc1;

namespace tt::umd {

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(
    std::unordered_set<chip_id_t> pci_target_devices, const std::string& sdesc_path) :
    TopologyDiscovery(pci_target_devices, sdesc_path) {}

std::unique_ptr<RemoteChip> TopologyDiscoveryBlackhole::create_remote_chip(
    eth_coord_t eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels) {
    return nullptr;
}

std::optional<eth_coord_t> TopologyDiscoveryBlackhole::get_local_eth_coord(Chip* chip) { return std::nullopt; }

std::optional<eth_coord_t> TopologyDiscoveryBlackhole::get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) {
    return std::nullopt;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_id(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint32_t board_id_lo;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&board_id_lo, translated_eth_core, 0x7CFE8, sizeof(board_id_lo));

    uint32_t board_id_hi;
    tt_device->read_from_device(&board_id_hi, translated_eth_core, 0x7CFE4, sizeof(board_id_hi));

    return (static_cast<uint64_t>(board_id_hi) << 32) | board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_board_id(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint32_t board_id_lo;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&board_id_lo, translated_eth_core, 0x7CFC8, sizeof(board_id_lo));

    uint32_t board_id_hi;
    tt_device->read_from_device(&board_id_hi, translated_eth_core, 0x7CFC4, sizeof(board_id_hi));

    return (static_cast<uint64_t>(board_id_hi) << 32) | board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint64_t board_id = get_local_board_id(chip, eth_core);

    uint8_t asic_location;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&asic_location, translated_eth_core, 0x7CFC1, sizeof(asic_location));

    return mangle_asic_id(board_id, asic_location);
}

uint64_t TopologyDiscoveryBlackhole::get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);

    uint64_t board_id = get_remote_board_id(chip, eth_core);

    uint8_t asic_location;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&asic_location, translated_eth_core, 0x7CFE1, sizeof(asic_location));

    return mangle_asic_id(board_id, asic_location);
}

tt_xy_pair TopologyDiscoveryBlackhole::get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) {
    throw std::runtime_error(
        "get_remote_eth_core is not implemented for Blackhole. Calling this function for Blackhole likely indicates a "
        "bug.");
}

uint32_t TopologyDiscoveryBlackhole::read_port_status(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t port_status;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&port_status, translated_eth_core, 0x7CC04, sizeof(port_status));
    return port_status;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        local_eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    TTDevice* tt_device = chip->get_tt_device();
    uint8_t remote_eth_id;
    tt_device->read_from_device(&remote_eth_id, translated_eth_core, 0x7CFE2, sizeof(remote_eth_id));
    return remote_eth_id;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_type(Chip* chip, tt_xy_pair eth_core) {
    // This function is not important for Blackhole, so we can return any value here.
    return 0;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) {
    return get_remote_eth_id(chip, local_eth_core);
}

bool TopologyDiscoveryBlackhole::is_using_eth_coords() { return false; }

bool TopologyDiscoveryBlackhole::is_board_id_included(uint64_t board_id, uint64_t board_type) const {
    return board_ids.find(board_id) != board_ids.end();
}

uint64_t TopologyDiscoveryBlackhole::mangle_asic_id(uint64_t board_id, uint8_t asic_location) {
    return ((board_id << 1) | (asic_location & 0x1));
}

bool TopologyDiscoveryBlackhole::is_eth_unconnected(Chip* chip, const tt_xy_pair eth_core) {
    return read_port_status(chip, eth_core) == blackhole::port_status_e::PORT_UNUSED;
}

bool TopologyDiscoveryBlackhole::is_eth_unknown(Chip* chip, const tt_xy_pair eth_core) {
    uint32_t port_status = read_port_status(chip, eth_core);
    return port_status == blackhole::port_status_e::PORT_UNKNOWN || port_status == blackhole::port_status_e::PORT_DOWN;
}

void TopologyDiscoveryBlackhole::patch_eth_connections() {
    std::set<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>> ethernet_connections_fixed;
    for (auto& eth_connections_original : ethernet_connections) {
        auto& [local_chip, local_channel] = eth_connections_original.first;
        auto& [remote_chip, remote_channel] = eth_connections_original.second;

        Chip* remote_chip_ptr = get_chip(remote_chip);

        auto eth_core_noc0 = tt::umd::blackhole::ETH_CORES_NOC0[remote_channel];
        CoreCoord eth_core_coord = CoreCoord(eth_core_noc0.x, eth_core_noc0.y, CoreType::ETH, CoordSystem::NOC0);
        CoreCoord logical_coord =
            remote_chip_ptr->get_soc_descriptor().translate_coord_to(eth_core_coord, CoordSystem::LOGICAL);

        ethernet_connections_fixed.insert({{local_chip, local_channel}, {remote_chip, logical_coord.y}});
    }

    ethernet_connections.clear();
    for (auto& eth_connections_fixed : ethernet_connections_fixed) {
        auto& [local_chip, local_channel] = eth_connections_fixed.first;
        auto& [remote_chip, remote_channel] = eth_connections_fixed.second;
        ethernet_connections.push_back({{local_chip, local_channel}, {remote_chip, remote_channel}});
    }
}

std::vector<uint32_t> TopologyDiscoveryBlackhole::extract_intermesh_eth_links(Chip* chip, tt_xy_pair eth_core) {
    // This function is not important for Blackhole.
    return {};
}

bool TopologyDiscoveryBlackhole::is_intermesh_eth_link_trained(Chip* chip, tt_xy_pair eth_core) {
    // This function is not important for Blackhole.
    return false;
}

}  // namespace tt::umd
