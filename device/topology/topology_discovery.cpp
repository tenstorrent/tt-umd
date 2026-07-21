// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "api/umd/device/topology/topology_discovery.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <utility>
#include <vector>

#include "api/umd/device/topology/topology_discovery_blackhole.hpp"
#include "api/umd/device/topology/topology_discovery_wormhole.hpp"
#include "tracy.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/topology/topology_discovery_error.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/topology/topology_utils.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"
#include "utils.hpp"

namespace tt::umd {

std::unique_ptr<TopologyDiscovery> TopologyDiscovery::create_topology_discovery(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    tt::ARCH current_arch = ARCH::Invalid;

    switch (io_device_type) {
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
                UMD_THROW(error::RuntimeError, "Blackhole architecture is not yet supported over JTAG interface.");
            }

            auto jtag_device = JtagDevice::create();
            if (!jtag_device->get_device_cnt()) {
                return nullptr;
            }
            current_arch = jtag_device->get_jtag_arch(0);
            break;
        }
        default:
            UMD_THROW(error::RuntimeError, "Unsupported device type for topology discovery.");
    }

    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor = nullptr;
    if (soc_descriptor_path.empty()) {
        soc_arch_descriptor = std::make_shared<SocArchDescriptor>(current_arch);
    } else {
        soc_arch_descriptor = std::make_shared<SocArchDescriptor>(soc_descriptor_path);
        if (soc_arch_descriptor->get_arch() != current_arch) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format(
                    "Architecture {} in SocArchDescriptor file on path {} does not match architecture {} on silicon.",
                    arch_to_str(soc_arch_descriptor->get_arch()),
                    soc_descriptor_path,
                    arch_to_str(current_arch)));
        }
    }

    log_info(LogUMD, "Creating TopologyDiscovery for architecture: {}", arch_to_str(current_arch));
    switch (current_arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<TopologyDiscoveryWormhole>(soc_arch_descriptor, options, io_device_type);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<TopologyDiscoveryBlackhole>(soc_arch_descriptor, options, io_device_type);
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Unsupported architecture for topology discovery."));
    }
}

TopologyDiscovery::TopologyDiscovery(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) :
    options(options), io_device_type(io_device_type), soc_arch_descriptor_(std::move(soc_arch_descriptor)) {}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    ZoneScopedC(tracy::Color::DarkGreen);
    log_info(LogUMD, "Starting topology discovery.");
    get_connected_devices();
    retrain_eth_cores();
    discover_remote_devices();
    log_info(LogUMD, "Completed topology discovery.");
    return fill_cluster_descriptor_info();
}

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    ZoneScopedC(tracy::Color::DarkGreen);
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    std::unique_ptr<TopologyDiscovery> td =
        TopologyDiscovery::create_topology_discovery(options, io_device_type, soc_descriptor_path);
    if (td == nullptr) {
        return std::make_pair(std::make_unique<ClusterDescriptor>(), std::move(devices));
    }
    std::unique_ptr<ClusterDescriptor> cluster_desc = td->create_ethernet_map();
    // Resort devices by ChipID instead of internal unique identifiers.
    for (auto& [unique_id, device] : td->devices) {
        ChipId chip_id = td->asic_id_to_chip_id[unique_id];
        devices[chip_id] = std::move(device);
    }
    return std::make_pair(std::move(cluster_desc), std::move(devices));
}

bool TopologyDiscovery::init_device(TTDevice* tt_device, ChipId chip_id, const std::chrono::milliseconds timeout) {
    try {
        tt_device->init_tt_device(timeout);
    } catch (error::UmdBaseException& err) {
        if (options.device_init_failure_action == TopologyDiscoveryOptions::Action::THROW) {
            throw;
        }
        log_warning(LogUMD, err.message());
        if (std::optional<ClusterDescriptor::DeviceHealthError> health_error = determine_device_init_error(err)) {
            health_errors[generate_unhealthy_asic_id(chip_id)].push_back(std::move(*health_error));
        }
        return false;
    }
    return true;
}

void TopologyDiscovery::get_connected_devices() {
    ZoneScopedC(tracy::Color::DarkGreen);
    std::vector<int> local_device_ids;
    switch (io_device_type) {
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
            UMD_THROW(error::RuntimeError, "Unsupported device type.");
    }

    for (auto& device_id : local_device_ids) {
        std::unique_ptr<TTDevice> tt_device =
            TTDevice::create(device_id, io_device_type, options.use_safe_api, soc_arch_descriptor_);
        if (options.low_power) {
            // Low power mode is temporarily disabled. See https://github.com/tenstorrent/tt-umd/issues/2531.
            log_warning(
                LogUMD,
                "Low power mode is not yet supported. The device will remain in high power mode while UMD holds open "
                "file descriptors.");
        } else {
            // set_power_state is currently a no-op until https://github.com/tenstorrent/tt-umd/issues/2531 is resolved.
            tt_device->set_power_state(true);
        }
        if (tt_device->get_arch() != get_topology_arch()) {
            log_warning(
                LogUMD,
                "Skipped device {} with different architecture: {}.",
                device_id,
                arch_to_str(tt_device->get_arch()));
            continue;
        }

        ChipId chip_id = get_next_chip_id();

        // When coming out of reset, devices can take on the order of minutes to become ready.
        if (!init_device(tt_device.get(), chip_id, timeout::ARC_LONG_POST_RESET_TIMEOUT)) {
            uint64_t asic_id = generate_unhealthy_asic_id(chip_id);
            devices_to_discover.emplace(asic_id, std::move(tt_device));
            asic_id_to_chip_id.emplace(asic_id, chip_id);

            log_warning(
                LogUMD,
                "Discovered unhealthy {} device w/ MMIO, ID: {}, mocked ASIC ID: {}",
                DeviceTypeToString.at(io_device_type),
                device_id,
                asic_id);
            continue;
        }

        // Check some things on first discovered MMIO device.
        if (devices_to_discover.empty()) {
            init_first_device(tt_device.get());
        }

        if (options.wait_on_ethernet_link_training) {
            wait_eth_cores_training(tt_device.get());
        }

        const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
        std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);
        for (const CoreCoord& eth_core : eth_cores) {
            uint64_t board_id = get_local_board_id(tt_device.get(), eth_core);
            if (board_id != 0) {
                board_ids.insert(board_id);
                break;
            }
        }

        uint64_t asic_id = get_asic_id(tt_device.get());
        devices_to_discover.emplace(asic_id, std::move(tt_device));
        asic_id_to_chip_id.emplace(asic_id, chip_id);

        log_debug(
            LogUMD,
            "Discovered {} device w/ MMIO, ID: {}, ASIC ID: {}",
            DeviceTypeToString.at(io_device_type),
            device_id,
            asic_id);
    }
    log_debug(LogUMD, "Discovered {} locally connected device(s).", devices_to_discover.size());
}

void TopologyDiscovery::discover_remote_devices() {
    ZoneScopedC(tracy::Color::DarkGreen);
    std::set<uint64_t> discovered_devices = {};
    for (const auto& [current_device_asic_id, tt_device] : devices_to_discover) {
        discovered_devices.insert(current_device_asic_id);
        remote_asic_id_to_mmio_device_id.emplace(current_device_asic_id, current_device_asic_id);
        active_eth_channels_per_device.emplace(current_device_asic_id, std::set<uint32_t>());
    }
    if (!options.discover_remote_devices) {
        log_debug(LogUMD, "Discovering remote devices is disabled.");
    }
    while (!devices_to_discover.empty()) {
        auto it = devices_to_discover.begin();
        uint64_t current_device_asic_id = it->first;
        devices.emplace(current_device_asic_id, std::move(it->second));
        devices_to_discover.erase(it);

        // Skip discovery from unhealthy devices.
        if (is_marked_unhealthy(current_device_asic_id)) {
            continue;
        }

        TTDevice* tt_device = devices.at(current_device_asic_id).get();

        verify_fw_bundle_version(tt_device, current_device_asic_id);

        if (!options.discover_remote_devices) {
            continue;
        }
        log_debug(LogUMD, "Discovering from ASIC ID: {}", current_device_asic_id);

        const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
        std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);
        for (const CoreCoord& eth_core : eth_cores) {
            const uint32_t channel = soc_desc.get_eth_channel_for_core(eth_core);

            if (is_eth_port_disabled(tt_device, eth_core)) {
                log_debug(
                    LogUMD,
                    "Skipping disabled ETH core {} on device ASIC ID: {} (port_disable_mask bit {} is set)",
                    eth_core.str(),
                    current_device_asic_id,
                    channel);
                continue;
            }

            const RiscType risc_reset_state = tt_device->get_architecture_implementation()->get_soft_reset_risc_type(
                tt_device->get_risc_reset_state(eth_core));
            if ((risc_reset_state & RiscType::ERISC0) != RiscType::NONE) {
                log_debug(
                    LogUMD,
                    "Skipping disabled ETH core {} on device ASIC ID: {} (ERISC0 reset bit is high)",
                    eth_core.str(),
                    current_device_asic_id);
                continue;
            }

            // TODO: Temporary - heartbeat check disabled for Blackhole.
            if (tt_device->get_arch() != ARCH::BLACKHOLE &&
                !eth_heartbeat_running(tt_device, current_device_asic_id, eth_core)) {
                auto err = UMD_THROW_OR_RETURN(
                    options.eth_fw_heartbeat_failure == TopologyDiscoveryOptions::Action::THROW,
                    error::RuntimeError,
                    fmt::format(
                        "ETH core heartbeat check failed on device ASIC ID: {}, ETH core {}, post code: {:x}",
                        current_device_asic_id,
                        eth_core.str(),
                        get_eth_postcode(tt_device, eth_core)));
                log_warning(LogUMD, err.message());
                continue;
            }

            if (!verify_eth_core_fw_version(tt_device, current_device_asic_id, eth_core)) {
                log_warning(
                    LogUMD,
                    "Skipping discovery from device ASIC ID: {} ETH core {}",
                    current_device_asic_id,
                    eth_core.str());

                continue;
            }

            if (is_using_eth_coords() && eth_coords.find(current_device_asic_id) == eth_coords.end()) {
                auto local_eth_coord = get_local_eth_coord(tt_device, eth_core);
                if (local_eth_coord.has_value()) {
                    eth_coords.emplace(current_device_asic_id, local_eth_coord.value());
                    log_debug(
                        LogUMD,
                        "Device ASIC ID: {} has ETH coord: {}",
                        current_device_asic_id,
                        local_eth_coord.value());
                }
            }

            if (!is_eth_trained(tt_device, eth_core)) {
                continue;
            }

            verify_routing_firmware_state(tt_device, current_device_asic_id, eth_core);

            log_debug(
                LogUMD,
                "Device ASIC ID: {} has active channel: {} ETH core: {}",
                current_device_asic_id,
                channel,
                eth_core.str());
            active_eth_channels_per_device.at(current_device_asic_id).insert(channel);
            uint64_t remote_asic_id = get_remote_asic_id(tt_device, eth_core);

            if (!is_board_id_included(get_remote_board_id(tt_device, eth_core)) ||
                (tt_device->get_arch() == ARCH::BLACKHOLE &&
                 discovered_devices.find(remote_asic_id) == discovered_devices.end())) {
                ethernet_connections_to_remote_devices.push_back(
                    {{current_device_asic_id, channel},
                     {remote_asic_id, get_logical_remote_eth_channel(tt_device, eth_core)}});
                log_debug(
                    LogUMD,
                    "Discovered remote device outside of host ASIC ID: {} over ETH core: {}",
                    remote_asic_id,
                    eth_core.str());

                continue;
            }

            if (discovered_devices.find(remote_asic_id) == discovered_devices.end()) {
                log_debug(
                    LogUMD, "Discovered remote device ASIC ID: {} over ETH core: {}", remote_asic_id, eth_core.str());
                uint64_t gateway_device_id = remote_asic_id_to_mmio_device_id.at(current_device_asic_id);
                std::optional<EthCoord> eth_coord = get_remote_eth_coord(tt_device, eth_core);
                std::unique_ptr<TTDevice> remote_device = create_remote_device(
                    eth_coord,
                    devices.at(gateway_device_id).get(),
                    active_eth_channels_per_device.at(gateway_device_id),
                    soc_arch_descriptor_);
                ChipId chip_id = get_next_chip_id();

                bool device_init_failed = !init_device(remote_device.get(), chip_id, timeout::ARC_STARTUP_TIMEOUT);
                if (device_init_failed) {
                    uint64_t mock_asic_id = generate_unhealthy_asic_id(chip_id);
                    devices_to_discover.emplace(mock_asic_id, std::move(remote_device));
                    asic_id_to_chip_id.emplace(mock_asic_id, chip_id);

                    log_warning(
                        LogUMD,
                        "Discovered remote device ASIC ID: {} is unhealthy. Assigned mock ASIC ID: {}",
                        remote_asic_id,
                        mock_asic_id);
                }
                if (!device_init_failed) {
                    if (options.wait_on_ethernet_link_training) {
                        wait_eth_cores_training(remote_device.get());
                    }
                    // Put device in discovery queue only if it is healthy.
                    devices_to_discover.emplace(remote_asic_id, std::move(remote_device));
                }

                // These actions are safe for both healthy and unhealthy devices.
                asic_id_to_chip_id.emplace(remote_asic_id, chip_id);
                active_eth_channels_per_device.emplace(remote_asic_id, std::set<uint32_t>());
                remote_asic_id_to_mmio_device_id.emplace(remote_asic_id, gateway_device_id);
                // This will prevent attempting init. of an unhealthy device over another ETH core.
                discovered_devices.insert(remote_asic_id);
            } else {
                log_debug(LogUMD, "Discovered link to ID: {} over ETH core: {}", remote_asic_id, eth_core.str());
                ethernet_connections.push_back(
                    {{current_device_asic_id, channel}, {remote_asic_id, get_remote_eth_channel(tt_device, eth_core)}});
            }
        }
    }

    patch_eth_connections();
}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::fill_cluster_descriptor_info() {
    std::unique_ptr<ClusterDescriptor> cluster_desc = std::make_unique<ClusterDescriptor>();

    for (const auto& [current_device_asic_id, tt_device] : devices) {
        ChipId chip_id = asic_id_to_chip_id[current_device_asic_id];

        if (is_marked_unhealthy(current_device_asic_id)) {
            cluster_desc->unhealthy_devices.push_back(chip_id);
            continue;
        }

        cluster_desc->chip_unique_ids.emplace(chip_id, current_device_asic_id);

        if (io_device_type == IODeviceType::PCIe && !tt_device->is_remote()) {
            cluster_desc->chip_pci_bdfs.emplace(chip_id, tt_device->get_pci_device()->get_device_info().pci_bdf);
        }

        if (eth_coords.empty()) {
            cluster_desc->closest_mmio_chip_cache[chip_id] =
                asic_id_to_chip_id.at(remote_asic_id_to_mmio_device_id.at(current_device_asic_id));
        }
    }

    for (const auto& [current_device_asic_id, tt_device] : devices) {
        ChipId current_chip_id = asic_id_to_chip_id.at(current_device_asic_id);

        cluster_desc->health_errors.insert({current_chip_id, std::move(health_errors[current_device_asic_id])});

        // Cluster descriptor is not designed to contain partial information about devices,
        // so we cannot add information about unhealthy devices.
        if (is_marked_unhealthy(current_device_asic_id)) {
            continue;
        }

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

    for (const auto& [current_chip_asic_id, active_eth_channels] : active_eth_channels_per_device) {
        if (is_marked_unhealthy(current_chip_asic_id)) {
            continue;
        }
        const uint32_t num_eth_channels =
            devices.at(current_chip_asic_id)->get_soc_descriptor().get_cores(CoreType::ETH).size();
        ChipId current_chip_id = asic_id_to_chip_id.at(current_chip_asic_id);
        for (int i = 0; i < num_eth_channels; i++) {
            cluster_desc->idle_eth_channels[current_chip_id].insert(i);
        }

        for (const auto& active_channel : active_eth_channels) {
            cluster_desc->active_eth_channels[current_chip_id].insert(active_channel);
            cluster_desc->idle_eth_channels[current_chip_id].erase(active_channel);
        }
    }
    cluster_desc->io_device_type = io_device_type;
    cluster_desc->eth_fw_version = expected_eth_fw_version;
    cluster_desc->fw_bundle_version = first_fw_bundle_version;
    cluster_desc->merge_cluster_ids();

    cluster_desc->fill_chips_grouped_by_closest_mmio();

    cluster_desc->verify_cluster_descriptor_info(options.discover_remote_devices);
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
    // and asic location from active (connected) ETH cores. If we have multiple ETH cores, we will use the first
    // one. If we have no ETH cores, we will use the board ID, since no other device can have the same board ID.
    // Using board ID should happen only for unconnected boards (N150, P150).
    const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
    std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);

    for (const CoreCoord& eth_core : eth_cores) {
        if (!is_eth_trained(tt_device, eth_core)) {
            continue;
        }

        return get_local_asic_id(tt_device, eth_core);
    }

    return get_unconnected_device_id(tt_device);
}

void TopologyDiscovery::patch_eth_connections() {}

void TopologyDiscovery::verify_fw_bundle_version(TTDevice* tt_device, uint64_t asic_id) {
    FirmwareBundleVersion fw_bundle_version = tt_device->get_firmware_version();

    if (first_fw_bundle_version.has_value()) {
        if (fw_bundle_version != first_fw_bundle_version.value()) {
            auto err = UMD_THROW_OR_RETURN(
                options.cmfw_mismatch_action == TopologyDiscoveryOptions::Action::THROW,
                error::CMFWMismatchError,
                *tt_device,
                asic_id,
                first_fw_bundle_version.value(),
                fw_bundle_version);
            log_warning(LogUMD, err.message());
            health_errors[asic_id].push_back(std::move(err));
        }
        return;
    }

    const tt::ARCH arch = tt_device->get_arch();
    first_fw_bundle_version = fw_bundle_version;
    log_info(LogUMD, "Established firmware bundle version: {}", fw_bundle_version.to_string());
    FirmwareBundleVersion minimum_compatible_fw_bundle_version =
        FirmwareInfoProvider::get_minimum_compatible_firmware_version(arch);
    FirmwareBundleVersion latest_supported_fw_bundle_version =
        FirmwareInfoProvider::get_latest_supported_firmware_version(arch);
    log_debug(
        LogUMD,
        "System firmware bundle version: {}. UMD supported firmware bundle versions: {} - {}.{}",
        fw_bundle_version.to_string(),
        minimum_compatible_fw_bundle_version.to_string(),
        latest_supported_fw_bundle_version.to_string(),
        fw_bundle_version > latest_supported_fw_bundle_version
            ? fmt::format(
                  " Firmware bundle version is newer than the latest fully tested version for {} architecture. Newest "
                  "features may not be supported.",
                  arch_to_str(arch))
            : "");

    if (fw_bundle_version < minimum_compatible_fw_bundle_version) {
        auto err = UMD_THROW_OR_RETURN(
            options.cmfw_unsupported_action == TopologyDiscoveryOptions::Action::THROW,
            error::UnsupportedCMFWError,
            *tt_device,
            asic_id,
            fw_bundle_version,
            minimum_compatible_fw_bundle_version);
        log_warning(LogUMD, err.message());
        health_errors[asic_id].push_back(std::move(err));
        return;
    }
}

void TopologyDiscovery::wait_eth_cores_training(TTDevice* tt_device, const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    log_debug(LogUMD, "Waiting on ethernet link training on device: {}", tt_device->get_communication_device_id());
    auto timeout_left = timeout_ms;
    const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
    const std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);
    for (const CoreCoord& eth_core : eth_cores) {
        tt_xy_pair actual_eth_core = soc_desc.translate_chip_coord_to_translated(eth_core);
        timeout_left -= tt_device->wait_eth_core_training(actual_eth_core, timeout_left);
    }
    log_debug(
        LogUMD,
        "Completed ethernet link training on device: {} after {} ms.",
        tt_device->get_communication_device_id(),
        (timeout_ms - timeout_left).count());
}

bool TopologyDiscovery::is_board_id_included(uint64_t board_id) const {
    return board_ids.find(board_id) != board_ids.end();
}

bool TopologyDiscovery::verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    SemVer eth_fw_version = get_eth_fw_version(tt_device, eth_core);

    bool eth_fw_problem = false;
    if (!expected_eth_fw_version.has_value()) {
        expected_eth_fw_version = tt_device->get_firmware_info_provider()->get_eth_fw_version_semver();
        if (expected_eth_fw_version.has_value()) {
            log_debug(LogUMD, "Expected ETH FW version from telemetry: {}", expected_eth_fw_version->to_string());
        } else {
            expected_eth_fw_version = eth_fw_version;
            log_debug(
                LogUMD, "Established ETH FW version from first discovered ETH core: {}", eth_fw_version.to_string());
        }

        SemVer minimum_supported = (get_topology_arch() == ARCH::BLACKHOLE)
                                       ? erisc_firmware::BH_MIN_ERISC_FW_SUPPORTED_VERSION
                                       : erisc_firmware::WH_MIN_ERISC_FW_SUPPORTED_VERSION;
        if (*expected_eth_fw_version < minimum_supported) {
            log_warning(
                LogUMD,
                "The expected ETH firmware version {} is older than the minimum supported version {}",
                expected_eth_fw_version->str(),
                minimum_supported.str());
            eth_fw_problem = true;
        }
    }

    if (eth_fw_version != *expected_eth_fw_version) {
        auto err = error::EthFirmwareMismatchError(
            *tt_device, asic_id, expected_eth_fw_version.value(), eth_fw_version, eth_core);
        log_warning(LogUMD, err.message());
        health_errors[asic_id].push_back(std::move(err));
        eth_fw_problem = true;
    }

    return (options.eth_fw_mismatch_action == TopologyDiscoveryOptions::Action::IGNORE) || !eth_fw_problem;
}

bool TopologyDiscovery::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    const auto start = std::chrono::steady_clock::now();
    uint32_t previous_reading = 0;
    // First loop: Wait until heartbeat changes from 0 (post reset).
    while (true) {
        uint32_t current_reading = get_eth_heartbeat(tt_device, eth_core);

        if (current_reading != 0) {
            previous_reading = current_reading;
            break;
        }

        if (utils::check_timeout(start, timeout::ETH_STARTUP_TIMEOUT)) {
            auto err = UMD_THROW_OR_RETURN(
                options.eth_fw_heartbeat_failure == TopologyDiscoveryOptions::Action::THROW,
                error::EthFirmwareHeartbeatError,
                *tt_device,
                asic_id,
                current_reading,
                eth_core);
            log_warning(LogUMD, err.message());
            health_errors[asic_id].push_back(std::move(err));
            return false;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    // Second loop: Wait for heartbeat to change.
    const auto second_start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t current_reading = get_eth_heartbeat(tt_device, eth_core);
        uint32_t signature = (current_reading >> 16);

        if (signature != erisc_firmware::BASE_FW_HEARTBEAT_SIGNATURE &&
            signature != erisc_firmware::FABRIC_HEARTBEAT_SIGNATURE) {
            log_warning(
                LogUMD,
                "Read invalid heartbeat signature: {:#x} from ETH core: {}, FW possibly corrupted.",
                current_reading,
                eth_core.str());
            return false;
        }

        if (previous_reading != current_reading) {
            return true;
        }

        if (utils::check_timeout(second_start, timeout::ETH_HEARTBEAT_TIMEOUT)) {
            auto err = UMD_THROW_OR_RETURN(
                options.eth_fw_heartbeat_failure == TopologyDiscoveryOptions::Action::THROW,
                error::EthFirmwareHeartbeatError,
                *tt_device,
                asic_id,
                current_reading,
                eth_core);
            log_warning(LogUMD, err.message());
            health_errors[asic_id].push_back(std::move(err));
            return false;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

bool TopologyDiscovery::is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core) {
    return tt_device->read_eth_core_training_status(eth_core) == EthTrainingStatus::SUCCESS;
}

}  // namespace tt::umd
