/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology_discovery.h"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/remote_chip.h"
#include "umd/device/remote_communication.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/tt_device/remote_wormhole_tt_device.h"
#include "umd/device/types/cluster_types.h"
#include "umd/device/types/wormhole_telemetry.h"
#include "umd/device/wormhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

std::unique_ptr<tt_ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    cluster_desc = std::unique_ptr<tt_ClusterDescriptor>(new tt_ClusterDescriptor());
    get_pcie_connected_chips();

    if (!chips.empty()) {
        eth_addresses = TopologyDiscovery::get_eth_addresses(
            chips.at(0)->get_tt_device()->get_arc_telemetry_reader()->read_entry(wormhole::TAG_ETH_FW_VERSION));
    }

    discover_remote_chips();
    fill_cluster_descriptor_info();
    return std::move(cluster_desc);
}

TopologyDiscovery::EthAddresses TopologyDiscovery::get_eth_addresses(uint32_t eth_fw_version) {
    uint32_t masked_version = eth_fw_version & 0x00FFFFFF;

    uint64_t version;
    uint64_t boot_params;
    uint64_t node_info;
    uint64_t eth_conn_info;
    uint64_t debug_buf;
    uint64_t results_buf;
    bool shelf_rack_routing;
    uint64_t heartbeat;
    uint64_t erisc_app;
    uint64_t erisc_app_config;
    uint64_t erisc_remote_board_type_offset;
    uint64_t erisc_local_board_type_offset;

    if (masked_version >= 0x050000) {
        boot_params = 0x1000;
        node_info = 0x1100;
        eth_conn_info = 0x1200;
        debug_buf = 0x12c0;
        results_buf = 0x1ec0;
        shelf_rack_routing = true;
    } else if (masked_version >= 0x030000) {
        boot_params = 0x1000;
        node_info = 0x1100;
        eth_conn_info = 0x1200;
        debug_buf = 0x1240;
        results_buf = 0x1e40;
        shelf_rack_routing = false;
    } else {
        boot_params = 0x5000;
        node_info = 0x5100;
        eth_conn_info = 0x5200;
        debug_buf = 0x5240;
        results_buf = 0x5e40;
        shelf_rack_routing = false;
    }

    if (masked_version >= 0x060000) {
        version = 0x210;
        heartbeat = 0x1c;
        erisc_app = 0x9040;
        erisc_app_config = 0x12000;
    } else {
        version = 0x210;
        heartbeat = 0x1f80;
        erisc_app = 0x8020;
        erisc_app_config = 0x12000;
    }

    if (masked_version >= 0x06C000) {
        erisc_remote_board_type_offset = 77;
        erisc_local_board_type_offset = 69;
    } else {
        erisc_remote_board_type_offset = 72;
        erisc_local_board_type_offset = 64;
    }

    return TopologyDiscovery::EthAddresses{
        masked_version,
        version,
        boot_params,
        node_info,
        eth_conn_info,
        debug_buf,
        results_buf,
        shelf_rack_routing,
        heartbeat,
        erisc_app,
        erisc_app_config,
        erisc_remote_board_type_offset,
        erisc_local_board_type_offset};
}

void TopologyDiscovery::get_pcie_connected_chips() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    chip_id = 0;
    for (auto& device_id : pci_device_ids) {
        std::unique_ptr<LocalChip> chip = nullptr;
        chip = std::make_unique<LocalChip>(TTDevice::create(device_id));
        chips.emplace(chip_id, std::move(chip));
        chip_id++;
    }
}

void TopologyDiscovery::discover_remote_chips() {
    const uint32_t eth_unknown = 0;
    const uint32_t eth_unconnected = 1;
    const uint32_t shelf_offset = 9;
    const uint32_t rack_offset = 10;

    std::unordered_map<uint64_t, chip_id_t> chip_uid_to_local_chip_id = {};

    std::unordered_set<eth_coord_t> discovered_chips = {};
    std::unordered_set<eth_coord_t> remote_chips_to_discover = {};

    for (const auto& [chip_id, chip] : chips) {
        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        TTDevice* tt_device = chip->get_tt_device();

        uint32_t current_chip_eth_coord_info;
        tt_device->read_from_device(
            &current_chip_eth_coord_info, eth_cores[0], eth_addresses.node_info + 8, sizeof(uint32_t));

        eth_coord_t current_chip_eth_coord;
        current_chip_eth_coord.cluster_id = 0;
        current_chip_eth_coord.x = (current_chip_eth_coord_info >> 16) & 0xFF;
        current_chip_eth_coord.y = (current_chip_eth_coord_info >> 24) & 0xFF;
        current_chip_eth_coord.rack = current_chip_eth_coord_info & 0xFF;
        current_chip_eth_coord.shelf = (current_chip_eth_coord_info >> 8) & 0xFF;

        eth_coords.emplace(chip_id, current_chip_eth_coord);
        eth_coord_to_chip_id.emplace(current_chip_eth_coord, chip_id);

        discovered_chips.insert(current_chip_eth_coord);
    }

    for (const auto& [chip_id, chip] : chips) {
        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        TTDevice* tt_device = chip->get_tt_device();

        uint32_t current_chip_eth_coord_info;
        tt_device->read_from_device(
            &current_chip_eth_coord_info, eth_cores[0], eth_addresses.node_info + 8, sizeof(uint32_t));

        eth_coord_t current_chip_eth_coord;
        current_chip_eth_coord.cluster_id = 0;
        current_chip_eth_coord.x = (current_chip_eth_coord_info >> 16) & 0xFF;
        current_chip_eth_coord.y = (current_chip_eth_coord_info >> 24) & 0xFF;
        current_chip_eth_coord.rack = current_chip_eth_coord_info & 0xFF;
        current_chip_eth_coord.shelf = (current_chip_eth_coord_info >> 8) & 0xFF;

        std::set<uint32_t> active_eth_channels;

        uint32_t channel = 0;
        for (const CoreCoord& eth_core : eth_cores) {
            uint32_t port_status;
            tt_device->read_from_device(
                &port_status,
                tt_cxy_pair(chip_id, eth_core.x, eth_core.y),
                eth_addresses.eth_conn_info + (channel * 4),
                sizeof(uint32_t));

            if (port_status == eth_unknown || port_status == eth_unconnected) {
                channel++;
                continue;
            }

            active_eth_channels.insert(channel);

            uint32_t remote_id;
            tt_device->read_from_device(
                &remote_id, {eth_core.x, eth_core.y}, eth_addresses.node_info + (4 * rack_offset), sizeof(uint32_t));

            uint32_t remote_rack_x = remote_id & 0xFF;
            uint32_t remote_rack_y = (remote_id >> 8) & 0xFF;

            tt_device->read_from_device(
                &remote_id, {eth_core.x, eth_core.y}, eth_addresses.node_info + (4 * shelf_offset), sizeof(uint32_t));

            uint32_t remote_shelf_x = (remote_id >> 16) & 0x3F;
            uint32_t remote_shelf_y = (remote_id >> 22) & 0x3F;

            uint32_t remote_noc_x = (remote_id >> 4) & 0x3F;
            uint32_t remote_noc_y = (remote_id >> 10) & 0x3F;

            eth_coord_t eth_coord;
            eth_coord.cluster_id = 0;
            eth_coord.x = remote_shelf_x;
            eth_coord.y = remote_shelf_y;
            eth_coord.rack = remote_rack_x;
            eth_coord.shelf = remote_rack_y;

            if (discovered_chips.find(eth_coord) == discovered_chips.end()) {
                remote_chips_to_discover.insert(eth_coord);
            } else {
                chip_id_t current_chip_id = eth_coord_to_chip_id.at(current_chip_eth_coord);
                chip_id_t remote_chip_id = eth_coord_to_chip_id.at(eth_coord);
                Chip* remote_chip = chips.at(remote_chip_id).get();
                CoreCoord physical_remote_eth =
                    CoreCoord(remote_noc_x, remote_noc_y, CoreType::ETH, CoordSystem::PHYSICAL);
                CoreCoord logical_remote_eth =
                    remote_chip->get_soc_descriptor().translate_coord_to(physical_remote_eth, CoordSystem::LOGICAL);
                ethernet_connections.push_back({{current_chip_id, channel}, {remote_chip_id, logical_remote_eth.y}});
            }
            channel++;
        }
        chip->set_remote_transfer_ethernet_cores(active_eth_channels);
    }

    if (remote_chips_to_discover.empty()) {
        return;
    }

    Chip* mmio_chip = chips.at(0).get();
    TTDevice* tt_device = mmio_chip->get_tt_device();
    std::unique_ptr<RemoteCommunication> remote_comm =
        std::make_unique<RemoteCommunication>(dynamic_cast<LocalChip*>(mmio_chip));

    while (!remote_chips_to_discover.empty()) {
        std::unordered_set<eth_coord_t> new_remote_chips = {};

        for (const eth_coord_t& eth_coord : remote_chips_to_discover) {
            std::vector<CoreCoord> eth_cores = mmio_chip->get_soc_descriptor().get_cores(
                CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);

            uint32_t current_chip_eth_coord_info;
            remote_comm->read_non_mmio(
                eth_coord, eth_cores[0], &current_chip_eth_coord_info, eth_addresses.node_info + 8, sizeof(uint32_t));

            eth_coord_t current_chip_eth_coord;
            current_chip_eth_coord.cluster_id = 0;
            current_chip_eth_coord.x = (current_chip_eth_coord_info >> 16) & 0xFF;
            current_chip_eth_coord.y = (current_chip_eth_coord_info >> 24) & 0xFF;
            current_chip_eth_coord.rack = current_chip_eth_coord_info & 0xFF;
            current_chip_eth_coord.shelf = (current_chip_eth_coord_info >> 8) & 0xFF;

            discovered_chips.insert(eth_coord);

            std::unique_ptr<RemoteWormholeTTDevice> remote_tt_device =
                std::make_unique<RemoteWormholeTTDevice>(dynamic_cast<LocalChip*>(mmio_chip), eth_coord);

            ChipInfo chip_info = remote_tt_device->get_chip_info();

            discovered_chips.insert(eth_coord);

            std::unique_ptr<RemoteChip> chip = nullptr;
            chip = std::make_unique<RemoteChip>(
                tt_SocDescriptor(
                    tt_device->get_arch(),
                    chip_info.noc_translation_enabled,
                    chip_info.harvesting_masks,
                    chip_info.board_type),
                std::move(remote_tt_device));

            chips.emplace(chip_id, std::move(chip));
            eth_coords.emplace(chip_id, current_chip_eth_coord);
            eth_coord_to_chip_id.emplace(current_chip_eth_coord, chip_id);
            chip_id++;

            uint32_t channel = 0;
            for (const CoreCoord& eth_core : eth_cores) {
                uint32_t port_status;
                remote_comm->read_non_mmio(
                    eth_coord,
                    tt_xy_pair(eth_core.x, eth_core.y),
                    &port_status,
                    eth_addresses.eth_conn_info + (channel * 4),
                    sizeof(uint32_t));

                if (port_status == eth_unknown || port_status == eth_unconnected) {
                    channel++;
                    continue;
                }

                uint32_t remote_id;
                remote_comm->read_non_mmio(
                    eth_coord,
                    {eth_core.x, eth_core.y},
                    &remote_id,
                    eth_addresses.node_info + (4 * rack_offset),
                    sizeof(uint32_t));

                uint32_t remote_rack_x = remote_id & 0xFF;
                uint32_t remote_rack_y = (remote_id >> 8) & 0xFF;

                remote_comm->read_non_mmio(
                    eth_coord,
                    {eth_core.x, eth_core.y},
                    &remote_id,
                    eth_addresses.node_info + (4 * shelf_offset),
                    sizeof(uint32_t));

                uint32_t remote_shelf_x = (remote_id >> 16) & 0x3F;
                uint32_t remote_shelf_y = (remote_id >> 22) & 0x3F;

                uint32_t remote_noc_x = (remote_id >> 4) & 0x3F;
                uint32_t remote_noc_y = (remote_id >> 10) & 0x3F;

                eth_coord_t new_eth_coord;
                new_eth_coord.cluster_id = 0;
                new_eth_coord.x = remote_shelf_x;
                new_eth_coord.y = remote_shelf_y;
                new_eth_coord.rack = remote_rack_x;
                new_eth_coord.shelf = remote_rack_y;

                if (discovered_chips.find(new_eth_coord) == discovered_chips.end()) {
                    if (remote_chips_to_discover.find(new_eth_coord) == remote_chips_to_discover.end()) {
                        new_remote_chips.insert(new_eth_coord);
                    }
                } else {
                    chip_id_t current_chip_id = eth_coord_to_chip_id.at(current_chip_eth_coord);
                    chip_id_t remote_chip_id = eth_coord_to_chip_id.at(new_eth_coord);
                    Chip* remote_chip = chips.at(remote_chip_id).get();
                    CoreCoord physical_remote_eth =
                        CoreCoord(remote_noc_x, remote_noc_y, CoreType::ETH, CoordSystem::PHYSICAL);
                    CoreCoord logical_remote_eth =
                        remote_chip->get_soc_descriptor().translate_coord_to(physical_remote_eth, CoordSystem::LOGICAL);
                    ethernet_connections.push_back(
                        {{current_chip_id, channel}, {remote_chip_id, logical_remote_eth.y}});
                }

                channel++;
            }
        }

        remote_chips_to_discover = new_remote_chips;
    }
}

void TopologyDiscovery::fill_cluster_descriptor_info() {
    for (const auto& [chip_id, chip] : chips) {
        cluster_desc->all_chips.insert(chip_id);
        cluster_desc->chip_arch.insert({chip_id, tt::ARCH::WORMHOLE_B0});

        if (chip->is_mmio_capable()) {
            cluster_desc->chips_with_mmio.insert({chip_id, chip->get_tt_device()->get_pci_device()->get_device_num()});
        }

        cluster_desc->chip_board_type.insert({chip_id, chip->get_chip_info().board_type});

        cluster_desc->noc_translation_enabled.insert({chip_id, chip->get_chip_info().noc_translation_enabled});
        cluster_desc->harvesting_masks.insert({chip_id, chip->get_chip_info().harvesting_masks.tensix_harvesting_mask});
        cluster_desc->dram_harvesting_masks.insert(
            {chip_id, chip->get_chip_info().harvesting_masks.dram_harvesting_mask});
        cluster_desc->eth_harvesting_masks.insert(
            {chip_id, chip->get_chip_info().harvesting_masks.eth_harvesting_mask});
        eth_coord_t eth_coord = eth_coords.at(chip_id);
        cluster_desc->chip_locations.insert({chip_id, eth_coord});
        cluster_desc->coords_to_chip_ids[eth_coord.rack][eth_coord.shelf][eth_coord.y][eth_coord.x] = chip_id;

        cluster_desc->add_chip_to_board(chip_id, chip->get_chip_info().chip_uid.board_id);

        for (int i = 0; i < wormhole::NUM_ETH_CHANNELS; i++) {
            cluster_desc->idle_eth_channels[chip_id].insert(i);
        }
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections) {
        cluster_desc->ethernet_connections[ethernet_connection_logical.first][ethernet_connection_logical.second] = {
            ethernet_connection_remote.first, ethernet_connection_remote.second};
        cluster_desc->ethernet_connections[ethernet_connection_remote.first][ethernet_connection_remote.second] = {
            ethernet_connection_logical.first, ethernet_connection_logical.second};
        cluster_desc->active_eth_channels[ethernet_connection_logical.first].insert(ethernet_connection_logical.second);
        cluster_desc->idle_eth_channels[ethernet_connection_logical.first].erase(ethernet_connection_logical.second);
        cluster_desc->active_eth_channels[ethernet_connection_remote.first].insert(ethernet_connection_remote.second);
        cluster_desc->idle_eth_channels[ethernet_connection_remote.first].erase(ethernet_connection_remote.second);
    }

    tt_ClusterDescriptor::fill_galaxy_connections(*cluster_desc.get());

    cluster_desc->fill_chips_grouped_by_closest_mmio();
}

}  // namespace tt::umd
