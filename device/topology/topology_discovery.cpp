// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/topology_discovery.hpp"

#include <memory>
#include <numeric>
#include <optional>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "api/umd/device/topology/topology_discovery.hpp"
#include "api/umd/device/topology/topology_discovery_blackhole.hpp"
#include "api/umd/device/topology/topology_discovery_wormhole.hpp"
#include "assert.hpp"
#include "noc_access.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

std::unique_ptr<TopologyDiscovery> TopologyDiscovery::create_topology_discovery(
    const TopologyDiscoveryOptions& options) {
    tt::ARCH current_arch = ARCH::Invalid;

    switch (options.io_device_type) {
        case IODeviceType::PCIe: {
            auto pci_devices_info = PCIDevice::enumerate_devices_info();
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
    log_debug(LogUMD, "Starting topology discovery.");
    init_topology_discovery();
    get_connected_devices();
    discover_remote_devices();
    log_debug(LogUMD, "Completed topology discovery.");
    return fill_cluster_descriptor_info();
}

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<uint64_t, std::unique_ptr<TTDevice>>>
TopologyDiscovery::discover(const TopologyDiscoveryOptions& options) {
    std::map<uint64_t, std::unique_ptr<TTDevice>> devices;
    std::unique_ptr<TopologyDiscovery> td = TopologyDiscovery::create_topology_discovery(options);
    if (td == nullptr) {
        return std::make_pair(std::make_unique<ClusterDescriptor>(), std::move(devices));
    }
    std::unique_ptr<ClusterDescriptor> cluster_desc = td->create_ethernet_map();
    return std::make_pair(std::move(cluster_desc), std::move(td->devices));
}

void TopologyDiscovery::init_topology_discovery() {}

void TopologyDiscovery::get_connected_devices() {
    std::vector<int> local_device_ids;
    switch (options.io_device_type) {
        case IODeviceType::PCIe: {
            local_device_ids = PCIDevice::enumerate_devices();
            break;
        }
        case IODeviceType::JTAG: {
            auto device_cnt = JtagDevice::create(JtagDevice::jtag_library_path)->get_device_cnt();
            local_device_ids = std::vector<int>(device_cnt);
            std::iota(local_device_ids.begin(), local_device_ids.end(), 0);
            break;
        }
        default:
            TT_THROW("Unsupported device type.");
    }
    for (auto& device_id : local_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(device_id, options.io_device_type);
        tt_device->init_tt_device();

        std::vector<CoreCoord> eth_cores =
            get_soc_descriptor(tt_device.get())
                .get_cores(CoreType::ETH, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);
        for (const CoreCoord& eth_core : eth_cores) {
            uint64_t board_id = get_local_board_id(tt_device.get(), eth_core);
            if (board_id != 0) {
                board_ids.insert(board_id);
                break;
            }
        }

        uint64_t asic_id = get_asic_id(tt_device.get());
        devices_to_discover.emplace(asic_id, std::move(tt_device));

        log_debug(
            LogUMD,
            "Discovered {} device w/ MMIO, ID: {}, ASIC ID {}",
            DeviceTypeToString.at(options.io_device_type),
            device_id,
            asic_id);
    }
}

void TopologyDiscovery::discover_remote_devices() {
    std::set<uint64_t> discovered_devices = {};
    for (const auto& [current_device_asic_id, tt_device] : devices_to_discover) {
        discovered_devices.insert(current_device_asic_id);
        remote_asic_id_to_mmio_device_id.emplace(current_device_asic_id, current_device_asic_id);
        active_eth_channels_per_device.emplace(current_device_asic_id, std::set<uint32_t>());
    }
    while (!devices_to_discover.empty()) {
        auto it = devices_to_discover.begin();
        uint64_t current_device_asic_id = it->first;
        devices.emplace(current_device_asic_id, std::move(it->second));
        devices_to_discover.erase(it);
        TTDevice* tt_device = devices.at(current_device_asic_id).get();

        if (options.no_remote_discovery) {
            continue;
        }

        std::vector<CoreCoord> eth_cores = get_soc_descriptor(tt_device).get_cores(
            CoreType::ETH, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);

        verify_fw_bundle_version(tt_device);

        uint32_t channel = 0;
        for (const CoreCoord& eth_core : eth_cores) {
            if (!verify_eth_core_fw_version(tt_device, eth_core)) {
                log_warning(
                    LogUMD,
                    "Skipping discovery from device {} ETH core {}",
                    get_local_asic_id(tt_device, eth_core),
                    eth_core.str());
                channel++;
                continue;
            }

            if (!is_eth_trained(tt_device, eth_core)) {
                channel++;
                continue;
            }

            if (!verify_routing_firmware_state(tt_device, eth_core)) {
                channel++;
                continue;
            }

            if (is_using_eth_coords()) {
                auto local_eth_coord = get_local_eth_coord(tt_device, eth_core);
                if (local_eth_coord.has_value() && eth_coords.find(current_device_asic_id) == eth_coords.end()) {
                    eth_coords.emplace(current_device_asic_id, local_eth_coord.value());
                    log_debug(LogUMD, "Device {} has ETH coord: {}", current_device_asic_id, local_eth_coord.value());
                }
            }
            active_eth_channels_per_device.at(current_device_asic_id).insert(channel);

            if (!is_board_id_included(
                    get_remote_board_id(tt_device, eth_core), get_remote_board_type(tt_device, eth_core)) ||
                (tt_device->get_arch() == ARCH::BLACKHOLE &&
                 discovered_devices.find(get_remote_asic_id(tt_device, eth_core)) == discovered_devices.end())) {
                uint64_t remote_asic_id = get_remote_asic_id(tt_device, eth_core);
                ethernet_connections_to_remote_devices.push_back(
                    {{current_device_asic_id, channel},
                     {remote_asic_id, get_logical_remote_eth_channel(tt_device, eth_core)}});
                log_debug(LogUMD, "Remote device outside of UMD cluster {}.", remote_asic_id);

                channel++;
                continue;
            }

            uint64_t remote_asic_id = get_remote_asic_id(tt_device, eth_core);

            if (discovered_devices.find(remote_asic_id) == discovered_devices.end()) {
                uint64_t gateway_device_id = remote_asic_id_to_mmio_device_id.at(current_device_asic_id);
                std::optional<EthCoord> eth_coord = get_remote_eth_coord(tt_device, eth_core);
                std::unique_ptr<TTDevice> remote_device = create_remote_device(
                    eth_coord,
                    devices.at(gateway_device_id).get(),
                    active_eth_channels_per_device.at(gateway_device_id));

                devices_to_discover.emplace(remote_asic_id, std::move(remote_device));
                active_eth_channels_per_device.emplace(remote_asic_id, std::set<uint32_t>());
                discovered_devices.insert(remote_asic_id);
                remote_asic_id_to_mmio_device_id.emplace(remote_asic_id, gateway_device_id);
                if (is_using_eth_coords()) {
                    eth_coords.emplace(remote_asic_id, eth_coord.value());
                }
            } else {
                ethernet_connections.push_back(
                    {{current_device_asic_id, channel}, {remote_asic_id, get_remote_eth_channel(tt_device, eth_core)}});
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
    for (const auto& [current_device_asic_id, tt_device] : devices) {
        if (!tt_device->is_remote()) {
            asic_id_to_chip_id.emplace(current_device_asic_id, chip_id);
            cluster_desc->chip_unique_ids.emplace(chip_id, current_device_asic_id);
            chip_id++;
        }
    }

    for (const auto& [current_device_asic_id, tt_device] : devices) {
        if (tt_device->is_remote()) {
            asic_id_to_chip_id.emplace(current_device_asic_id, chip_id);
            cluster_desc->chip_unique_ids.emplace(chip_id, current_device_asic_id);
            if (eth_coords.empty()) {
                cluster_desc->closest_mmio_chip_cache[chip_id] =
                    asic_id_to_chip_id.at(remote_asic_id_to_mmio_device_id.at(current_device_asic_id));
            }
            chip_id++;
        }
    }

    for (const auto& [current_device_asic_id, tt_device] : devices) {
        ChipId current_chip_id = asic_id_to_chip_id.at(current_device_asic_id);
        cluster_desc->all_chips.insert(current_chip_id);
        cluster_desc->chip_arch.insert({current_chip_id, tt_device->get_arch()});

        if (!tt_device->is_remote()) {
            cluster_desc->chips_with_mmio.insert({current_chip_id, tt_device->get_communication_device_id()});
        }

        cluster_desc->chip_board_type.insert({current_chip_id, tt_device->get_chip_info().board_type});

        cluster_desc->noc_translation_enabled.insert(
            {current_chip_id, tt_device->get_chip_info().noc_translation_enabled});
        cluster_desc->harvesting_masks_map.insert({current_chip_id, tt_device->get_chip_info().harvesting_masks});
        cluster_desc->asic_locations.insert({current_chip_id, tt_device->get_chip_info().asic_location});

        if (tt_device->get_pci_device()) {
            cluster_desc->chip_to_bus_id.insert(
                {current_chip_id, tt_device->get_pci_device()->get_device_info().pci_bus});
        }

        if (is_using_eth_coords()) {
            if (!eth_coords.empty()) {
                EthCoord eth_coord = eth_coords.at(current_device_asic_id);
                cluster_desc->chip_locations.insert({current_chip_id, eth_coord});
                cluster_desc->coords_to_chip_ids[eth_coord.rack][eth_coord.shelf][eth_coord.y][eth_coord.x] =
                    current_chip_id;
            }
        }

        cluster_desc->add_chip_to_board(current_chip_id, tt_device->get_chip_info().board_id);
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

    const uint32_t num_eth_channels = get_soc_descriptor(devices.begin()->second.get()).get_cores(CoreType::ETH).size();
    for (auto [current_chip_asic_id, active_eth_channels] : active_eth_channels_per_device) {
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
    cluster_desc->eth_fw_version = expected_eth_fw_version;
    cluster_desc->fill_galaxy_connections();
    cluster_desc->merge_cluster_ids();

    cluster_desc->fill_chips_grouped_by_closest_mmio();

    cluster_desc->verify_cluster_descriptor_info();
    return cluster_desc;
}

TTDevice* TopologyDiscovery::get_tt_device(const uint64_t asic_id) {
    if (devices_to_discover.find(asic_id) != devices_to_discover.end()) {
        return devices_to_discover.at(asic_id).get();
    }
    return devices.at(asic_id).get();
}

uint64_t TopologyDiscovery::get_asic_id(TTDevice* tt_device) {
    // This function should return a unique ID for the device. At the moment we are going to use mangled board ID
    // and asic location from active (connected) ETH cores. If we have multiple ETH cores, we will use the first one.
    // If we have no ETH cores, we will use the board ID, since no other device can have the same board ID.
    // Using board ID should happen only for unconnected boards (N150, P150).
    std::vector<CoreCoord> eth_cores = get_soc_descriptor(tt_device).get_cores(
        CoreType::ETH, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);

    for (const CoreCoord& eth_core : eth_cores) {
        if (!is_eth_trained(tt_device, eth_core)) {
            continue;
        }

        return get_local_asic_id(tt_device, eth_core);
    }

    return get_unconnected_device_id(tt_device);
}

void TopologyDiscovery::patch_eth_connections() {}

bool TopologyDiscovery::verify_fw_bundle_version(TTDevice* tt_device) {
    semver_t fw_bundle_version = tt_device->get_firmware_version();

    if (first_fw_bundle_version.has_value()) {
        if (fw_bundle_version != first_fw_bundle_version.value()) {
            log_warning(
                LogUMD,
                fmt::format(
                    "Firmware bundle version mismatch for device {}: expected {}, got {}",
                    get_asic_id(tt_device),
                    fw_bundle_version.to_string(),
                    tt_device->get_firmware_version().to_string()));
            return false;
        }
        return true;
    }

    first_fw_bundle_version = fw_bundle_version;
    log_info(LogUMD, "Established firmware bundle version: {}", fw_bundle_version.to_string());
    semver_t minimum_compatible_fw_bundle_version =
        FirmwareInfoProvider::get_minimum_compatible_firmware_version(tt_device->get_arch());
    semver_t latest_supported_fw_bundle_version =
        FirmwareInfoProvider::get_latest_supported_firmware_version(tt_device->get_arch());
    log_debug(
        LogUMD,
        "UMD supported firmware bundle versions: {} - {}",
        minimum_compatible_fw_bundle_version.to_string(),
        latest_supported_fw_bundle_version.to_string());

    TT_ASSERT(
        semver_t::compare_firmware_bundle(fw_bundle_version, minimum_compatible_fw_bundle_version) >= 0,
        "Firmware bundle version {} on the system is older than the minimum compatible version {} for {} "
        "architecture.",
        fw_bundle_version.to_string(),
        minimum_compatible_fw_bundle_version.to_string(),
        arch_to_str(tt_device->get_arch()));

    if (semver_t::compare_firmware_bundle(fw_bundle_version, latest_supported_fw_bundle_version) > 0) {
        log_info(
            LogUMD,
            "Firmware bundle version {} on the system is newer than the latest fully tested version {} for {} "
            "architecture. Newest features may not be supported.",
            fw_bundle_version.to_string(),
            latest_supported_fw_bundle_version.to_string(),
            arch_to_str(tt_device->get_arch()));
    }
    return true;
}

SocDescriptor TopologyDiscovery::get_soc_descriptor(TTDevice* tt_device) {
    // HACK: This methods shows that SocDescriptor is needed with almost every use of
    // TTDevice in TopologyDiscovery, so the SocDescriptor itself should be owned by
    // TTDevice. This method caches SocDescriptors to reduce the overhead of creating one
    // on the spot every time.
    auto it = soc_descriptor_cache.find(tt_device);
    if (it != soc_descriptor_cache.end()) {
        return it->second;
    }

    SocDescriptor soc_descriptor;
    if (options.soc_descriptor_path.empty()) {
        // In case soc descriptor yaml wasn't passed, we create soc descriptor with default values for the architecture.
        soc_descriptor = SocDescriptor(tt_device->get_arch(), tt_device->get_chip_info());
    } else {
        soc_descriptor = SocDescriptor::create_from_yaml(options.soc_descriptor_path, tt_device->get_chip_info());
    }

    soc_descriptor_cache[tt_device] = soc_descriptor;
    return soc_descriptor;
}

}  // namespace tt::umd
