/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery.hpp"

#include <memory>
#include <numeric>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "api/umd/device/topology/topology_discovery.hpp"
#include "api/umd/device/topology/topology_discovery_blackhole.hpp"
#include "api/umd/device/topology/topology_discovery_wormhole.hpp"
#include "assert.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/cluster_descriptor.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

std::unique_ptr<TopologyDiscovery> TopologyDiscovery::create_topology_discovery(
    const TopologyDiscoveryOptions& options) {
    tt::ARCH current_arch = ARCH::Invalid;

    switch (options.io_device_type) {
        case IODeviceType::PCIe: {
            auto pci_devices_info = PCIDevice::enumerate_devices_info(options.target_devices);
            if (pci_devices_info.empty()) {
                return nullptr;
            }
            current_arch = pci_devices_info.begin()->second.get_arch();
            break;
        }
        case IODeviceType::JTAG: {
            if (current_arch == tt::ARCH::BLACKHOLE) {
                TT_THROW("Blackhole architecture is not yet supported over JTAG interface.");
            }

            auto jtag_device = JtagDevice::create();
            if (!jtag_device->get_device_cnt()) {
                return nullptr;
            }
            current_arch = jtag_device->get_jtag_arch(0);
            break;
        }
        default:
            TT_THROW("Unsupported device type for topology discovery");
    }

    switch (current_arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<TopologyDiscoveryWormhole>(options);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<TopologyDiscoveryBlackhole>(options);
        default:
            throw std::runtime_error(fmt::format("Unsupported architecture for topology discovery."));
    }
}

TopologyDiscovery::TopologyDiscovery(const TopologyDiscoveryOptions& options) : options(options) {}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    init_topology_discovery();
    get_connected_chips();
    discover_remote_chips();
    return fill_cluster_descriptor_info();
}

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<uint64_t, std::unique_ptr<Chip>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options) {
    std::map<uint64_t, std::unique_ptr<Chip>> chips;
    std::unique_ptr<TopologyDiscovery> td = TopologyDiscovery::create_topology_discovery(options);
    if (td == nullptr) {
        return std::make_pair(std::make_unique<ClusterDescriptor>(), std::move(chips));
    }
    std::unique_ptr<ClusterDescriptor> cluster_desc = td->create_ethernet_map();
    return std::make_pair(std::move(cluster_desc), std::move(td->chips));
}

void TopologyDiscovery::init_topology_discovery() {}

void TopologyDiscovery::get_connected_chips() {
    std::vector<int> device_ids;
    switch (options.io_device_type) {
        case IODeviceType::PCIe: {
            device_ids = PCIDevice::enumerate_devices(options.target_devices);
            break;
        }
        case IODeviceType::JTAG: {
            auto device_cnt =
                JtagDevice::create(JtagDevice::jtag_library_path, options.target_devices)->get_device_cnt();
            device_ids = std::vector<int>(device_cnt);
            std::iota(device_ids.begin(), device_ids.end(), 0);
            break;
        }
        default:
            TT_THROW("Unsupported device type.");
    }
    for (auto& device_id : device_ids) {
        std::unique_ptr<LocalChip> chip =
            LocalChip::create(device_id, options.soc_descriptor_path, 0, options.io_device_type);

        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        for (const CoreCoord& eth_core : eth_cores) {
            uint64_t board_id = get_local_board_id(chip.get(), eth_core);
            if (board_id != 0) {
                board_ids.insert(board_id);
                break;
            }
        }

        initialize_remote_communication(chip.get());
        uint64_t asic_id = get_asic_id(chip.get());
        chips_to_discover.emplace(asic_id, std::move(chip));
        log_debug(
            LogUMD,
            "Discovered {} chip with {} ID {} and asic ID {}",
            DeviceTypeToString.at(options.io_device_type),
            DeviceTypeToString.at(options.io_device_type),
            device_id,
            asic_id);
    }
}

void TopologyDiscovery::discover_remote_chips() {
    std::set<uint64_t> discovered_chips = {};

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

        uint32_t channel = 0;
        for (const CoreCoord& eth_core : eth_cores) {
            if (!verify_eth_core_fw_version(chip, eth_core)) {
                log_warning(
                    LogUMD,
                    "Skipping discovery from chip {} ETH core {}",
                    get_local_asic_id(chip, eth_core),
                    eth_core.str());
                channel++;
                continue;
            }

            if (!is_eth_trained(chip, eth_core)) {
                channel++;
                continue;
            }
            active_eth_channels_per_chip.at(current_chip_asic_id).insert(channel);

            if (!is_board_id_included(get_remote_board_id(chip, eth_core), get_remote_board_type(chip, eth_core))) {
                uint64_t remote_asic_id = get_remote_asic_id(chip, eth_core);
                if (chip->get_chip_info().board_type == BoardType::P150) {
                    ethernet_connections_to_remote_devices.push_back(
                        {{current_chip_asic_id, channel},
                         {remote_asic_id, get_logical_remote_eth_channel(chip, eth_core)}});
                } else {
                    ethernet_connections_to_remote_devices.push_back(
                        {{current_chip_asic_id, channel}, {remote_asic_id, get_remote_eth_channel(chip, eth_core)}});
                }
                log_debug(LogUMD, "Remote chip outside of UMD cluster {}.", remote_asic_id);

                channel++;
                continue;
            }

            uint64_t remote_asic_id = get_remote_asic_id(chip, eth_core);

            if (discovered_chips.find(remote_asic_id) == discovered_chips.end()) {
                uint64_t gateway_chip_id = remote_asic_id_to_mmio_chip_id.at(current_chip_asic_id);
                std::optional<EthCoord> eth_coord = get_remote_eth_coord(chip, eth_core);
                std::unique_ptr<Chip> remote_chip = create_remote_chip(
                    eth_coord, chips.at(gateway_chip_id).get(), active_eth_channels_per_chip.at(gateway_chip_id));

                chips_to_discover.emplace(remote_asic_id, std::move(remote_chip));
                active_eth_channels_per_chip.emplace(remote_asic_id, std::set<uint32_t>());
                discovered_chips.insert(remote_asic_id);
                remote_asic_id_to_mmio_chip_id.emplace(remote_asic_id, gateway_chip_id);
                if (is_using_eth_coords()) {
                    eth_coords.emplace(remote_asic_id, eth_coord.value());
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

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::fill_cluster_descriptor_info() {
    std::unique_ptr<ClusterDescriptor> cluster_desc = std::make_unique<ClusterDescriptor>();
    std::map<uint64_t, ChipId> asic_id_to_chip_id;
    ChipId chip_id = 0;
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
            if (eth_coords.empty()) {
                cluster_desc->closest_mmio_chip_cache[chip_id] =
                    asic_id_to_chip_id.at(remote_asic_id_to_mmio_chip_id.at(current_chip_asic_id));
            }
            chip_id++;
        }
    }

    for (const auto& [current_chip_asic_id, chip] : chips) {
        ChipId current_chip_id = asic_id_to_chip_id.at(current_chip_asic_id);
        cluster_desc->all_chips.insert(current_chip_id);
        cluster_desc->chip_arch.insert({current_chip_id, chip->get_tt_device()->get_arch()});

        if (chip->is_mmio_capable()) {
            cluster_desc->chips_with_mmio.insert(
                {current_chip_id, chip->get_tt_device()->get_communication_device_id()});
        }

        cluster_desc->chip_board_type.insert({current_chip_id, chip->get_chip_info().board_type});

        cluster_desc->noc_translation_enabled.insert({current_chip_id, chip->get_chip_info().noc_translation_enabled});
        cluster_desc->harvesting_masks_map.insert({current_chip_id, chip->get_chip_info().harvesting_masks});
        cluster_desc->asic_locations.insert({current_chip_id, chip->get_tt_device()->get_chip_info().asic_location});

        if (chip->get_tt_device()->get_pci_device()) {
            cluster_desc->chip_to_bus_id.insert(
                {current_chip_id, chip->get_tt_device()->get_pci_device()->get_device_info().pci_bus});
        }

        if (is_using_eth_coords()) {
            if (!eth_coords.empty()) {
                EthCoord eth_coord = eth_coords.at(current_chip_asic_id);
                cluster_desc->chip_locations.insert({current_chip_id, eth_coord});
                cluster_desc->coords_to_chip_ids[eth_coord.rack][eth_coord.shelf][eth_coord.y][eth_coord.x] =
                    current_chip_id;
            }
        }

        cluster_desc->add_chip_to_board(current_chip_id, chip->get_chip_info().board_id);
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections) {
        ChipId local_chip_id = asic_id_to_chip_id.at(ethernet_connection_logical.first);
        ChipId remote_chip_id = asic_id_to_chip_id.at(ethernet_connection_remote.first);
        cluster_desc->ethernet_connections[local_chip_id][ethernet_connection_logical.second] = {
            remote_chip_id, ethernet_connection_remote.second};
        cluster_desc->ethernet_connections[remote_chip_id][ethernet_connection_remote.second] = {
            local_chip_id, ethernet_connection_logical.second};
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections_to_remote_devices) {
        ChipId local_chip_id = asic_id_to_chip_id.at(ethernet_connection_logical.first);
        cluster_desc->ethernet_connections_to_remote_devices[local_chip_id][ethernet_connection_logical.second] = {
            ethernet_connection_remote.first, ethernet_connection_remote.second};
    }

    const uint32_t num_eth_channels = chips.begin()->second->get_soc_descriptor().get_cores(CoreType::ETH).size();
    for (auto [current_chip_asic_id, active_eth_channels] : active_eth_channels_per_chip) {
        ChipId current_chip_id = asic_id_to_chip_id.at(current_chip_asic_id);
        for (int i = 0; i < num_eth_channels; i++) {
            cluster_desc->idle_eth_channels[current_chip_id].insert(i);
        }

        for (const auto& active_channel : active_eth_channels) {
            cluster_desc->active_eth_channels[current_chip_id].insert(active_channel);
            cluster_desc->idle_eth_channels[current_chip_id].erase(active_channel);
        }
    }
    cluster_desc->io_device_type = options.io_device_type;
    cluster_desc->eth_fw_version = first_eth_fw_version;
    cluster_desc->fill_galaxy_connections();
    cluster_desc->merge_cluster_ids();

    cluster_desc->fill_chips_grouped_by_closest_mmio();

    cluster_desc->verify_cluster_descriptor_info();
    return cluster_desc;
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
        if (!is_eth_trained(chip, eth_core)) {
            continue;
        }

        return get_local_asic_id(chip, eth_core);
    }

    return get_unconnected_chip_id(chip);
}

void TopologyDiscovery::patch_eth_connections() {}

void TopologyDiscovery::initialize_remote_communication(Chip* chip) {}

}  // namespace tt::umd
