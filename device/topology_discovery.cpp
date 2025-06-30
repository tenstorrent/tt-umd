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

TopologyDiscovery::TopologyDiscovery(std::unordered_set<chip_id_t> pci_target_devices) :
    pci_target_devices(pci_target_devices) {}

// Functions called by create_ethernet_map should stay same for all configs as much as possible.
// We should try and override functions for getting data from ETH core, creating remote communication etc..
// For all the different configs we have (T3K, 6U, BH)...
std::unique_ptr<tt_ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    cluster_desc = std::unique_ptr<tt_ClusterDescriptor>(new tt_ClusterDescriptor());
    get_pcie_connected_chips();
    discover_remote_chips();
    fill_cluster_descriptor_info();
    return std::move(cluster_desc);
}

TopologyDiscovery::EthAddresses TopologyDiscovery::get_eth_addresses(uint32_t eth_fw_version) {
    uint32_t masked_version = eth_fw_version & 0x00FFFFFF;

    uint64_t node_info;
    uint64_t eth_conn_info;
    uint64_t results_buf;
    uint64_t erisc_remote_board_type_offset;
    uint64_t erisc_local_board_type_offset;
    uint64_t erisc_local_board_id_lo_offset;
    uint64_t erisc_remote_board_id_lo_offset;

    if (masked_version >= 0x060000) {
        node_info = 0x1100;
        eth_conn_info = 0x1200;
        results_buf = 0x1ec0;
    } else {
        throw std::runtime_error(
            fmt::format("Unsupported ETH version {:#x}. ETH version should always be at least 6.0.0.", eth_fw_version));
    }

    if (masked_version >= 0x06C000) {
        erisc_remote_board_type_offset = 77;
        erisc_local_board_type_offset = 69;
        erisc_remote_board_id_lo_offset = 72;
        erisc_local_board_id_lo_offset = 64;
    } else {
        erisc_remote_board_type_offset = 72;
        erisc_local_board_type_offset = 64;
        erisc_remote_board_id_lo_offset = 73;
        erisc_local_board_id_lo_offset = 65;
    }

    return TopologyDiscovery::EthAddresses{
        masked_version,
        node_info,
        eth_conn_info,
        results_buf,
        erisc_remote_board_type_offset,
        erisc_local_board_type_offset,
        erisc_local_board_id_lo_offset,
        erisc_remote_board_id_lo_offset};
}

std::unique_ptr<RemoteWormholeTTDevice> TopologyDiscovery::create_remote_tt_device(
    Chip* chip, tt_xy_pair eth_core, Chip* gateway_chip) {
    return std::make_unique<RemoteWormholeTTDevice>(
        dynamic_cast<LocalChip*>(gateway_chip), get_remote_eth_coord(chip, eth_core));
}

eth_coord_t TopologyDiscovery::get_local_eth_coord(Chip* chip) {
    std::vector<CoreCoord> eth_cores =
        chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
    TTDevice* tt_device = chip->get_tt_device();

    uint32_t current_chip_eth_coord_info;
    tt_device->read_from_device(
        &current_chip_eth_coord_info, eth_cores[0], eth_addresses.node_info + 8, sizeof(uint32_t));

    eth_coord_t eth_coord;
    eth_coord.cluster_id = 0;
    eth_coord.x = (current_chip_eth_coord_info >> 16) & 0xFF;
    eth_coord.y = (current_chip_eth_coord_info >> 24) & 0xFF;
    eth_coord.rack = current_chip_eth_coord_info & 0xFF;
    eth_coord.shelf = (current_chip_eth_coord_info >> 8) & 0xFF;

    return eth_coord;
}

eth_coord_t TopologyDiscovery::get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) {
    const uint32_t shelf_offset = 9;
    const uint32_t rack_offset = 10;
    TTDevice* tt_device = chip->get_tt_device();
    eth_coord_t eth_coord;
    eth_coord.cluster_id = 0;
    uint32_t remote_id;
    tt_device->read_from_device(
        &remote_id, {eth_core.x, eth_core.y}, eth_addresses.node_info + (4 * rack_offset), sizeof(uint32_t));

    eth_coord.rack = remote_id & 0xFF;
    eth_coord.shelf = (remote_id >> 8) & 0xFF;

    tt_device->read_from_device(
        &remote_id, {eth_core.x, eth_core.y}, eth_addresses.node_info + (4 * shelf_offset), sizeof(uint32_t));

    eth_coord.x = (remote_id >> 16) & 0x3F;
    eth_coord.y = (remote_id >> 22) & 0x3F;

    return eth_coord;
}

void TopologyDiscovery::get_pcie_connected_chips() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    chip_id = 0;
    for (auto& device_id : pci_device_ids) {
        if (!is_pcie_chip_id_included(device_id)) {
            continue;
        }
        std::unique_ptr<LocalChip> chip = nullptr;
        chip = std::make_unique<LocalChip>(TTDevice::create(device_id));

        // ETH addresses need to be initialized after the first chip is created, so we could
        // read the information about offsets of board IDs on ETH core.
        // TODO: confirm that we should only support one set of addresses so we can remove
        // figuring out ETH addresses from runtime and move it to constants.
        if (chip_id == 0) {
            eth_addresses = TopologyDiscovery::get_eth_addresses(
                chip->get_tt_device()->get_arc_telemetry_reader()->read_entry(wormhole::TAG_ETH_FW_VERSION));
        }

        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        for (const CoreCoord& eth_core : eth_cores) {
            uint32_t board_id = get_local_board_id(chip.get(), eth_core);
            if (board_id == 0) {
                continue;
            }
            board_ids.insert(board_id);
        }
        chips.emplace(chip_id, std::move(chip));
        chip_id++;
    }
}

uint64_t TopologyDiscovery::get_asic_id(Chip* chip) {
    // This function should return a unique ID for the chip. At the moment we are going to use mangled board ID
    // and asic location from active (connected) ETH cores. If we have multiple ETH cores, we will use the first one.
    // If we have no ETH cores, we will use the board ID, since no other chip can have the same board ID.
    // Using board ID should happen only for unconnected N150.
    const uint32_t eth_unknown = 0;
    const uint32_t eth_unconnected = 1;
    std::vector<CoreCoord> eth_cores =
        chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);

    uint32_t channel = 0;
    for (const CoreCoord& eth_core : eth_cores) {
        uint32_t port_status = read_port_status(chip, eth_core, channel);

        if (port_status == eth_unknown || port_status == eth_unconnected) {
            channel++;
            continue;
        }

        return get_local_asic_id(chip, eth_core);
    }

    return chip->get_tt_device()->get_board_id();
}

void TopologyDiscovery::discover_remote_chips() {
    const uint32_t eth_unknown = 0;
    const uint32_t eth_unconnected = 1;
    const uint32_t rack_offset = 10;

    std::unordered_map<uint64_t, chip_id_t> chip_uid_to_local_chip_id = {};

    std::unordered_set<uint64_t> discovered_chips = {};
    std::unordered_set<uint64_t> remote_chips_to_discover = {};
    std::unordered_map<uint64_t, std::unique_ptr<RemoteWormholeTTDevice>> remote_tt_devices_to_discover = {};
    // Needed to know which chip to use for remote communication.
    std::unordered_map<uint64_t, chip_id_t> remote_asic_id_to_mmio_chip_id = {};

    for (const auto& [chip_id, chip] : chips) {
        uint64_t current_chip_asic_id = get_asic_id(chip.get());

        asic_id_to_chip_id.emplace(current_chip_asic_id, chip_id);

        discovered_chips.insert(current_chip_asic_id);

        // TODO: this neeeds to be moved to specific logic for Wormhole with legacy FW.
        eth_coords.emplace(chip_id, get_local_eth_coord(chip.get()));
    }

    for (const auto& [chip_id, chip] : chips) {
        active_eth_channels_per_chip.emplace(chip_id, std::set<uint32_t>());
        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        TTDevice* tt_device = chip->get_tt_device();

        uint64_t current_chip_asic_id = get_asic_id(chip.get());

        uint32_t channel = 0;
        for (const CoreCoord& eth_core : eth_cores) {
            uint32_t port_status = read_port_status(chip.get(), eth_core, channel);

            if (port_status == eth_unknown || port_status == eth_unconnected) {
                channel++;
                continue;
            }

            active_eth_channels_per_chip.at(chip_id).insert(channel);

            if (!is_board_id_included(get_remote_board_id(chip.get(), eth_core))) {
                channel++;
                continue;
            }

            chip->set_remote_transfer_ethernet_cores(active_eth_channels_per_chip.at(chip_id));

            std::unique_ptr<RemoteWormholeTTDevice> remote_tt_device =
                create_remote_tt_device(chip.get(), eth_core, chip.get());

            tt_xy_pair remote_eth_core = get_remote_eth_core(chip.get(), eth_core);
            uint64_t remote_asic_id = get_remote_asic_id(chip.get(), eth_core);

            if (discovered_chips.find(remote_asic_id) == discovered_chips.end()) {
                remote_chips_to_discover.insert(remote_asic_id);
                remote_tt_devices_to_discover.emplace(remote_asic_id, std::move(remote_tt_device));
                remote_asic_id_to_mmio_chip_id.emplace(remote_asic_id, chip_id);
            } else {
                chip_id_t remote_chip_id = asic_id_to_chip_id.at(remote_asic_id);
                Chip* remote_chip = chips.at(remote_chip_id).get();
                CoreCoord physical_remote_eth =
                    CoreCoord(remote_eth_core.x, remote_eth_core.y, CoreType::ETH, CoordSystem::PHYSICAL);
                CoreCoord logical_remote_eth =
                    remote_chip->get_soc_descriptor().translate_coord_to(physical_remote_eth, CoordSystem::LOGICAL);
                ethernet_connections.push_back({{chip_id, channel}, {remote_chip_id, logical_remote_eth.y}});
            }
            channel++;
        }
        chip->set_remote_transfer_ethernet_cores(active_eth_channels_per_chip.at(chip_id));
    }

    if (remote_chips_to_discover.empty()) {
        return;
    }

    while (!remote_chips_to_discover.empty()) {
        std::unordered_set<uint64_t> new_remote_chips = {};
        std::unordered_map<uint64_t, std::unique_ptr<RemoteWormholeTTDevice>> new_tt_devices = {};

        for (const uint64_t asic_id_to_discover : remote_chips_to_discover) {
            chip_id_t mmio_chip_id = remote_asic_id_to_mmio_chip_id.at(asic_id_to_discover);
            Chip* mmio_chip = chips.at(mmio_chip_id).get();
            std::unique_ptr<RemoteWormholeTTDevice>& remote_tt_device =
                remote_tt_devices_to_discover.at(asic_id_to_discover);
            std::vector<CoreCoord> eth_cores = mmio_chip->get_soc_descriptor().get_cores(
                CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);

            discovered_chips.insert(asic_id_to_discover);

            ChipInfo chip_info = remote_tt_device->get_chip_info();

            std::unique_ptr<RemoteChip> chip = nullptr;
            chip = std::make_unique<RemoteChip>(
                tt_SocDescriptor(
                    remote_tt_device->get_arch(),
                    chip_info.noc_translation_enabled,
                    chip_info.harvesting_masks,
                    chip_info.board_type),
                std::move(remote_tt_device));

            Chip* remote_chip_ptr = chip.get();

            // TODO: this neeeds to be moved to specific logic for Wormhole with legacy FW.
            eth_coords.emplace(chip_id, get_local_eth_coord(remote_chip_ptr));

            TTDevice* remote_tt_device_ptr = chip->get_tt_device();

            chips.emplace(chip_id, std::move(chip));
            active_eth_channels_per_chip.emplace(chip_id, std::set<uint32_t>());
            asic_id_to_chip_id.emplace(asic_id_to_discover, chip_id);
            chip_id++;

            uint32_t channel = 0;
            for (const CoreCoord& eth_core : eth_cores) {
                uint32_t port_status = read_port_status(remote_chip_ptr, eth_core, channel);

                if (port_status == eth_unknown || port_status == eth_unconnected) {
                    channel++;
                    continue;
                }

                active_eth_channels_per_chip.at(chip_id - 1).insert(channel);

                if (!is_board_id_included(get_remote_board_id(chips.at(chip_id - 1).get(), eth_core))) {
                    channel++;
                    continue;
                }

                tt_xy_pair remote_eth_core = get_remote_eth_core(chips.at(chip_id - 1).get(), eth_core);

                uint64_t new_asic_id = get_remote_asic_id(chips.at(chip_id - 1).get(), {eth_core.x, eth_core.y});

                if (discovered_chips.find(new_asic_id) == discovered_chips.end()) {
                    if (remote_chips_to_discover.find(new_asic_id) == remote_chips_to_discover.end()) {
                        std::unique_ptr<RemoteWormholeTTDevice> new_remote_tt_device =
                            create_remote_tt_device(chips.at(chip_id - 1).get(), {eth_core.x, eth_core.y}, mmio_chip);
                        new_remote_chips.insert(new_asic_id);
                        new_tt_devices.emplace(new_asic_id, std::move(new_remote_tt_device));
                        remote_asic_id_to_mmio_chip_id.emplace(new_asic_id, mmio_chip_id);
                    }
                } else {
                    chip_id_t current_chip_id = asic_id_to_chip_id.at(asic_id_to_discover);
                    chip_id_t remote_chip_id = asic_id_to_chip_id.at(new_asic_id);
                    Chip* remote_chip = chips.at(remote_chip_id).get();
                    CoreCoord physical_remote_eth =
                        CoreCoord(remote_eth_core.x, remote_eth_core.y, CoreType::ETH, CoordSystem::PHYSICAL);
                    CoreCoord logical_remote_eth =
                        remote_chip->get_soc_descriptor().translate_coord_to(physical_remote_eth, CoordSystem::LOGICAL);
                    ethernet_connections.push_back(
                        {{current_chip_id, channel}, {remote_chip_id, logical_remote_eth.y}});
                }
                channel++;
            }
        }

        remote_chips_to_discover = new_remote_chips;
        remote_tt_devices_to_discover.clear();
        remote_tt_devices_to_discover = std::move(new_tt_devices);
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
        cluster_desc->harvesting_masks_map.insert({chip_id, chip->get_chip_info().harvesting_masks});
        // TODO: this neeeds to be moved to specific logic for Wormhole with legacy FW.
        eth_coord_t eth_coord = eth_coords.at(chip_id);
        cluster_desc->chip_locations.insert({chip_id, eth_coord});
        cluster_desc->coords_to_chip_ids[eth_coord.rack][eth_coord.shelf][eth_coord.y][eth_coord.x] = chip_id;

        cluster_desc->add_chip_to_board(chip_id, chip->get_chip_info().chip_uid.board_id);
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections) {
        cluster_desc->ethernet_connections[ethernet_connection_logical.first][ethernet_connection_logical.second] = {
            ethernet_connection_remote.first, ethernet_connection_remote.second};
        cluster_desc->ethernet_connections[ethernet_connection_remote.first][ethernet_connection_remote.second] = {
            ethernet_connection_logical.first, ethernet_connection_logical.second};
    }

    for (auto [chip_id, active_eth_channels] : active_eth_channels_per_chip) {
        for (int i = 0; i < wormhole::NUM_ETH_CHANNELS; i++) {
            cluster_desc->idle_eth_channels[chip_id].insert(i);
        }

        for (const auto& active_channel : active_eth_channels) {
            cluster_desc->active_eth_channels[chip_id].insert(active_channel);
            cluster_desc->idle_eth_channels[chip_id].erase(active_channel);
        }
    }

    tt_ClusterDescriptor::fill_galaxy_connections(*cluster_desc.get());
    tt_ClusterDescriptor::merge_cluster_ids(*cluster_desc.get());

    cluster_desc->fill_chips_grouped_by_closest_mmio();
}

// If pci_target_devices is empty, we should take all the PCI devices found in the system.
// That is why we have the first part of the condition.
bool TopologyDiscovery::is_pcie_chip_id_included(int pci_id) const {
    return pci_target_devices.empty() || pci_target_devices.find(pci_id) != pci_target_devices.end();
}

// If pci_target_devices is empty, we should take all the PCI devices found in the system.
// That is why we have the first part of the condition.
bool TopologyDiscovery::is_board_id_included(uint32_t board_id) const {
    return pci_target_devices.empty() || board_ids.find(board_id) != board_ids.end();
}

uint32_t TopologyDiscovery::get_remote_board_id(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_remote_board_id_lo_offset),
        sizeof(uint32_t));
    return board_id;
}

uint32_t TopologyDiscovery::get_local_board_id(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_local_board_id_lo_offset),
        sizeof(uint32_t));
    return board_id;
}

uint64_t TopologyDiscovery::get_local_asic_id(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t asic_id_lo;
    tt_device->read_from_device(
        &asic_id_lo,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_local_board_id_lo_offset),
        sizeof(uint32_t));
    uint32_t asic_id_hi;
    tt_device->read_from_device(
        &asic_id_hi,
        eth_core,
        eth_addresses.results_buf + (4 * (eth_addresses.erisc_local_board_id_lo_offset + 1)),
        sizeof(uint32_t));
    return ((static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo);
}

uint64_t TopologyDiscovery::get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t asic_id_lo;
    tt_device->read_from_device(
        &asic_id_lo,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_remote_board_id_lo_offset),
        sizeof(uint32_t));
    uint32_t asic_id_hi;
    tt_device->read_from_device(
        &asic_id_hi,
        eth_core,
        eth_addresses.results_buf + (4 * (eth_addresses.erisc_remote_board_id_lo_offset + 1)),
        sizeof(uint32_t));
    return ((static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo);
}

tt_xy_pair TopologyDiscovery::get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) {
    const uint32_t shelf_offset = 9;
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t remote_id;
    tt_device->read_from_device(
        &remote_id,
        {local_eth_core.x, local_eth_core.y},
        eth_addresses.node_info + (4 * shelf_offset),
        sizeof(uint32_t));

    return tt_xy_pair{(remote_id >> 4) & 0x3F, (remote_id >> 10) & 0x3F};
}

uint32_t TopologyDiscovery::read_port_status(Chip* chip, tt_xy_pair eth_core, uint32_t channel) {
    uint32_t port_status;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        &port_status,
        tt_cxy_pair(chip_id, eth_core.x, eth_core.y),
        eth_addresses.eth_conn_info + (channel * 4),
        sizeof(uint32_t));
    return port_status;
}

}  // namespace tt::umd
