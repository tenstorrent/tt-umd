/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery.h"

#include <tt-logger/tt-logger.hpp>

#include "api/umd/device/topology/topology_discovery.h"
#include "api/umd/device/topology/topology_discovery_blackhole.h"
#include "api/umd/device/topology/topology_discovery_wormhole.h"
#include "umd/device/arch/wormhole_implementation.h"
#include "umd/device/chip/local_chip.h"
#include "umd/device/cluster_descriptor.h"
#include "umd/device/tt_device/remote_communication.h"
#include "umd/device/tt_device/remote_wormhole_tt_device.h"
#include "umd/device/types/cluster_types.h"

extern bool umd_use_noc1;

namespace tt::umd {

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_cluster_descriptor(
    std::unordered_set<chip_id_t> pci_target_devices, const std::string& sdesc_path) {
    auto pci_devices_info = PCIDevice::enumerate_devices_info(pci_target_devices);
    if (pci_devices_info.empty()) {
        return std::make_unique<ClusterDescriptor>();
    }

    switch (pci_devices_info.begin()->second.get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            return TopologyDiscoveryWormhole(pci_target_devices, sdesc_path).create_ethernet_map();
        case tt::ARCH::BLACKHOLE:
            return TopologyDiscoveryBlackhole(pci_target_devices, sdesc_path).create_ethernet_map();
        default:
            throw std::runtime_error(fmt::format("Unsupported architecture for topology discovery."));
    }
}

TopologyDiscovery::TopologyDiscovery(std::unordered_set<chip_id_t> pci_target_devices, const std::string& sdesc_path) :
    pci_target_devices(pci_target_devices), sdesc_path(sdesc_path) {}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    init_topology_discovery();
    cluster_desc = std::unique_ptr<ClusterDescriptor>(new ClusterDescriptor());
    get_pcie_connected_chips();
    discover_remote_chips();
    fill_cluster_descriptor_info();
    return std::move(cluster_desc);
}

void TopologyDiscovery::init_topology_discovery() {}

void TopologyDiscovery::get_pcie_connected_chips() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices(pci_target_devices);

    for (auto& device_id : pci_device_ids) {
        std::unique_ptr<LocalChip> chip = LocalChip::create(device_id, sdesc_path);

        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        for (const CoreCoord& eth_core : eth_cores) {
            uint64_t board_id = get_local_board_id(chip.get(), eth_core);
            if (board_id != 0) {
                board_ids.insert(board_id);
                break;
            }
        }
        uint64_t asic_id = get_asic_id(chip.get());
        chips_to_discover.emplace(asic_id, std::move(chip));
        log_debug(LogSiliconDriver, "Discovered PCI chip with PCI ID {} and asic ID {}", device_id, asic_id);
    }
}

void TopologyDiscovery::discover_remote_chips() {
    std::set<uint64_t> discovered_chips = {};
    // Needed to know which chip to use for remote communication.
    std::map<uint64_t, uint64_t> remote_asic_id_to_mmio_chip_id = {};

    for (const auto& [current_chip_asic_id, chip] : chips_to_discover) {
        discovered_chips.insert(current_chip_asic_id);

        remote_asic_id_to_mmio_chip_id.emplace(current_chip_asic_id, current_chip_asic_id);

        active_eth_channels_per_chip.emplace(current_chip_asic_id, std::set<uint32_t>());

        if (is_using_eth_coords()) {
            auto local_eth_coord = get_local_eth_coord(chip.get());
            if (local_eth_coord.has_value()) {
                eth_coords.emplace(current_chip_asic_id, local_eth_coord.value());
            }
        }
    }

    while (!chips_to_discover.empty()) {
        auto it = chips_to_discover.begin();
        uint64_t current_chip_asic_id = it->first;
        chips.emplace(current_chip_asic_id, std::move(it->second));
        chips_to_discover.erase(it);
        Chip* chip = chips.at(current_chip_asic_id).get();

        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        TTDevice* tt_device = chip->get_tt_device();

        std::vector<uint32_t> intermesh_eth_links;
        if (eth_cores.size() > 0) {
            intermesh_eth_links = extract_intermesh_eth_links(chip, eth_cores.front());
        }

        uint32_t channel = 0;
        for (const CoreCoord& eth_core : eth_cores) {
            uint32_t port_status = read_port_status(chip, eth_core);

            if (is_eth_unknown(chip, eth_core) || is_eth_unconnected(chip, eth_core)) {
                if (std::find(intermesh_eth_links.begin(), intermesh_eth_links.end(), channel) ==
                    intermesh_eth_links.end()) {
                    channel++;
                    continue;
                }
                if (!is_intermesh_eth_link_trained(chip, eth_core)) {
                    channel++;
                    continue;
                }
            }

            active_eth_channels_per_chip.at(current_chip_asic_id).insert(channel);

            if (!is_board_id_included(get_remote_board_id(chip, eth_core), get_remote_board_type(chip, eth_core))) {
                uint64_t remote_asic_id = get_remote_asic_id(chip, eth_core);
                ethernet_connections_to_remote_devices.push_back(
                    {{current_chip_asic_id, channel}, {remote_asic_id, get_remote_eth_channel(chip, eth_core)}});

                log_debug(LogSiliconDriver, "Remote chip outside of UMD cluster {}.", remote_asic_id);

                channel++;
                continue;
            }

            uint64_t remote_asic_id = get_remote_asic_id(chip, eth_core);

            if (discovered_chips.find(remote_asic_id) == discovered_chips.end()) {
                uint64_t gateway_chip_id = remote_asic_id_to_mmio_chip_id.at(current_chip_asic_id);
                eth_coord_t eth_coord = get_remote_eth_coord(chip, eth_core).value();
                std::unique_ptr<Chip> remote_chip = create_remote_chip(
                    eth_coord, chips.at(gateway_chip_id).get(), active_eth_channels_per_chip.at(gateway_chip_id));

                chips_to_discover.emplace(remote_asic_id, std::move(remote_chip));
                active_eth_channels_per_chip.emplace(remote_asic_id, std::set<uint32_t>());
                discovered_chips.insert(remote_asic_id);
                remote_asic_id_to_mmio_chip_id.emplace(remote_asic_id, gateway_chip_id);
                if (is_using_eth_coords()) {
                    eth_coords.emplace(remote_asic_id, get_remote_eth_coord(chip, eth_core).value());
                }
            } else {
                ethernet_connections.push_back(
                    {{current_chip_asic_id, channel}, {remote_asic_id, get_remote_eth_channel(chip, eth_core)}});
            }
            channel++;
        }
    }

    patch_eth_connections();
}

void TopologyDiscovery::fill_cluster_descriptor_info() {
    std::map<uint64_t, chip_id_t> asic_id_to_chip_id;
    chip_id_t chip_id = 0;
    for (const auto& [current_chip_asic_id, chip] : chips) {
        if (chip->is_mmio_capable()) {
            asic_id_to_chip_id.emplace(current_chip_asic_id, chip_id);
            cluster_desc->chip_unique_ids.emplace(chip_id, current_chip_asic_id);
            chip_id++;
        }
    }

    for (const auto& [current_chip_asic_id, chip] : chips) {
        if (!chip->is_mmio_capable()) {
            asic_id_to_chip_id.emplace(current_chip_asic_id, chip_id);
            cluster_desc->chip_unique_ids.emplace(chip_id, current_chip_asic_id);
            chip_id++;
        }
    }

    for (const auto& [current_chip_asic_id, chip] : chips) {
        chip_id_t current_chip_id = asic_id_to_chip_id.at(current_chip_asic_id);
        cluster_desc->all_chips.insert(current_chip_id);
        cluster_desc->chip_arch.insert({current_chip_id, chip->get_tt_device()->get_arch()});

        if (chip->is_mmio_capable()) {
            cluster_desc->chips_with_mmio.insert(
                {current_chip_id, chip->get_tt_device()->get_pci_device()->get_device_num()});
        }

        cluster_desc->chip_board_type.insert({current_chip_id, chip->get_chip_info().board_type});

        cluster_desc->noc_translation_enabled.insert({current_chip_id, chip->get_chip_info().noc_translation_enabled});
        cluster_desc->harvesting_masks_map.insert({current_chip_id, chip->get_chip_info().harvesting_masks});
        cluster_desc->asic_locations.insert({current_chip_id, chip->get_tt_device()->get_chip_info().asic_location});

        if (is_using_eth_coords()) {
            if (!eth_coords.empty()) {
                eth_coord_t eth_coord = eth_coords.at(current_chip_asic_id);
                cluster_desc->chip_locations.insert({current_chip_id, eth_coord});
                cluster_desc->coords_to_chip_ids[eth_coord.rack][eth_coord.shelf][eth_coord.y][eth_coord.x] =
                    current_chip_id;
            }
        }

        cluster_desc->add_chip_to_board(current_chip_id, chip->get_chip_info().chip_uid.board_id);
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections) {
        chip_id_t local_chip_id = asic_id_to_chip_id.at(ethernet_connection_logical.first);
        chip_id_t remote_chip_id = asic_id_to_chip_id.at(ethernet_connection_remote.first);
        cluster_desc->ethernet_connections[local_chip_id][ethernet_connection_logical.second] = {
            remote_chip_id, ethernet_connection_remote.second};
        cluster_desc->ethernet_connections[remote_chip_id][ethernet_connection_remote.second] = {
            local_chip_id, ethernet_connection_logical.second};
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections_to_remote_devices) {
        chip_id_t local_chip_id = asic_id_to_chip_id.at(ethernet_connection_logical.first);
        cluster_desc->ethernet_connections_to_remote_devices[local_chip_id][ethernet_connection_logical.second] = {
            ethernet_connection_remote.first, ethernet_connection_remote.second};
    }

    const uint32_t num_eth_channels = chips.begin()->second->get_soc_descriptor().get_cores(CoreType::ETH).size();
    for (auto [current_chip_asic_id, active_eth_channels] : active_eth_channels_per_chip) {
        chip_id_t current_chip_id = asic_id_to_chip_id.at(current_chip_asic_id);
        for (int i = 0; i < num_eth_channels; i++) {
            cluster_desc->idle_eth_channels[current_chip_id].insert(i);
        }

        for (const auto& active_channel : active_eth_channels) {
            cluster_desc->active_eth_channels[current_chip_id].insert(active_channel);
            cluster_desc->idle_eth_channels[current_chip_id].erase(active_channel);
        }
    }

    cluster_desc->fill_galaxy_connections();
    cluster_desc->merge_cluster_ids();

    cluster_desc->fill_chips_grouped_by_closest_mmio();

    cluster_desc->verify_cluster_descriptor_info();
}

Chip* TopologyDiscovery::get_chip(const uint64_t asic_id) {
    if (chips_to_discover.find(asic_id) != chips_to_discover.end()) {
        return chips_to_discover.at(asic_id).get();
    }
    return chips.at(asic_id).get();
}

uint64_t TopologyDiscovery::get_asic_id(Chip* chip) {
    // This function should return a unique ID for the chip. At the moment we are going to use mangled board ID
    // and asic location from active (connected) ETH cores. If we have multiple ETH cores, we will use the first one.
    // If we have no ETH cores, we will use the board ID, since no other chip can have the same board ID.
    // Using board ID should happen only for unconnected boards (N150, P150).
    std::vector<CoreCoord> eth_cores =
        chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);

    for (const CoreCoord& eth_core : eth_cores) {
        uint32_t port_status = read_port_status(chip, eth_core);

        if (is_eth_unknown(chip, eth_core) || is_eth_unconnected(chip, eth_core)) {
            continue;
        }

        return get_local_asic_id(chip, eth_core);
    }

    return chip->get_tt_device()->get_board_id();
}

void TopologyDiscovery::patch_eth_connections() {}

}  // namespace tt::umd
