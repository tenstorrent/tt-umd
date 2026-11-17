// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery.hpp"

#include <fmt/format.h>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "common/utils.hpp"
#include "topology/topology_discovery_blackhole.hpp"
#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Applies a TopologyDiscoveryOptions::Action to a problem found on one device: THROW raises it, IGNORE logs it and
// records it against the device, so it reaches the cluster descriptor's health errors.
template <typename Error>
void report_discovery_issue(
    TopologyDiscoveryOptions::Action action,
    const Error& error,
    uint64_t asic_id,
    std::map<uint64_t, std::vector<ClusterDescriptor::DeviceHealthError>>& health_errors) {
    if (action == TopologyDiscoveryOptions::Action::THROW) {
        throw error::UmdException<Error>(error, __FILE__, __LINE__);
    }
    log_warning(LogUMD, "{}", error.message());
    health_errors[asic_id].push_back(error);
}

}  // namespace

TopologyDiscovery::TopologyDiscovery(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) :
    options(options), io_device_type(io_device_type), soc_arch_descriptor_(std::move(soc_arch_descriptor)) {}

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    std::unique_ptr<TopologyDiscovery> topology_discovery =
        create_topology_discovery(options, io_device_type, soc_descriptor_path);
    if (topology_discovery == nullptr) {
        // Callers go through discovery on machines without devices too, so they get a valid, chipless descriptor.
        auto cluster_descriptor = std::make_unique<ClusterDescriptor>();
        cluster_descriptor->cluster_id = utils::resolve_cluster_id(options.cluster_id);
        cluster_descriptor->io_device_type = io_device_type;
        return {std::move(cluster_descriptor), std::map<ChipId, std::unique_ptr<TTDevice>>()};
    }

    std::unique_ptr<ClusterDescriptor> cluster_descriptor = topology_discovery->create_ethernet_map();
    std::map<ChipId, std::unique_ptr<TTDevice>> tt_devices;
    for (auto& [asic_id, tt_device] : topology_discovery->devices) {
        tt_devices.emplace(topology_discovery->asic_id_to_chip_id.at(asic_id), std::move(tt_device));
    }
    return {std::move(cluster_descriptor), std::move(tt_devices)};
}

std::unique_ptr<TopologyDiscovery> TopologyDiscovery::create_topology_discovery(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    UMD_ASSERT(!options.simulation.has_value(), error::RuntimeError, "Discovering simulated devices is not supported.");
    UMD_ASSERT(
        io_device_type == IODeviceType::PCIe,
        error::RuntimeError,
        fmt::format("Topology discovery over {} is not supported.", DeviceTypeToString.at(io_device_type)));

    const std::map<int, PciDeviceInfo> pci_devices = PCIDevice::enumerate_devices_info();
    if (pci_devices.empty()) {
        return nullptr;
    }

    const tt::ARCH arch = pci_devices.begin()->second.get_arch();
    UMD_ASSERT(
        arch == tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        fmt::format("Topology discovery is not supported for {} devices.", arch_to_str(arch)));

    auto soc_arch_descriptor = soc_descriptor_path.empty() ? std::make_shared<SocArchDescriptor>(arch)
                                                           : std::make_shared<SocArchDescriptor>(soc_descriptor_path);
    return std::make_unique<TopologyDiscoveryBlackhole>(std::move(soc_arch_descriptor), options, io_device_type);
}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    get_connected_devices();
    patch_eth_connections();
    return fill_cluster_descriptor_info();
}

void TopologyDiscovery::get_connected_devices() {
    // Visible devices only: enumeration applies TT_VISIBLE_DEVICES, and chip ids follow its (BDF) order.
    for (const int pci_device_id : PCIDevice::enumerate_devices()) {
        add_local_device(
            TTDevice::create(pci_device_id, io_device_type, options.use_safe_api, soc_arch_descriptor_), pci_device_id);
    }

    // Every local device is known before any link is walked, so a link between two of them is told apart from a link
    // leaving the host.
    for (auto& [asic_id, tt_device] : devices_to_discover) {
        if (is_marked_unhealthy(asic_id)) {
            continue;
        }
        const SocDescriptor& soc_descriptor = tt_device->get_soc_descriptor();
        for (uint32_t channel = 0; channel < soc_descriptor.get_num_eth_channels(); channel++) {
            const CoreCoord eth_core = soc_descriptor.get_eth_core_for_channel(channel, CoordSystem::NOC0);
            // Every core runs firmware whether its link trained or not, and clients use idle cores too, so a crashed
            // core is reported even when it has no link.
            if (is_eth_port_disabled(tt_device.get(), eth_core) ||
                !eth_heartbeat_running(tt_device.get(), asic_id, eth_core)) {
                continue;
            }
            // A mismatch the options ignore is recorded and the core keeps serving as usual.
            verify_eth_core_fw_version(tt_device.get(), asic_id, eth_core);
            if (!is_eth_trained(tt_device.get(), eth_core)) {
                continue;
            }
            verify_routing_firmware_state(tt_device.get(), asic_id, eth_core);
            active_eth_channels_per_device[asic_id].insert(channel);

            const uint64_t remote_asic_id = get_remote_asic_id(tt_device.get(), eth_core);
            if (devices_to_discover.count(remote_asic_id) != 0) {
                ethernet_connections.push_back(
                    {{asic_id, channel}, {remote_asic_id, get_remote_eth_channel(tt_device.get(), eth_core)}});
            } else {
                ethernet_connections_to_remote_devices.push_back(
                    {{asic_id, channel}, {remote_asic_id, get_logical_remote_eth_channel(tt_device.get(), eth_core)}});
            }
        }
    }
    devices.merge(devices_to_discover);
}

// Blackhole devices are reached only over PCIe, so no device sits behind an Ethernet link of a local one.
void TopologyDiscovery::discover_remote_devices() {}

// Without remote devices there are no tunnels to spread over gateway links.
void TopologyDiscovery::tunnel_remote_devices_over_all_gateway_links() {}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::fill_cluster_descriptor_info() {
    auto cluster_desc = std::make_unique<ClusterDescriptor>();
    cluster_desc->cluster_id = utils::resolve_cluster_id(options.cluster_id);
    cluster_desc->io_device_type = io_device_type;
    cluster_desc->authentic_chip_unique_ids = true;
    cluster_desc->eth_fw_version = expected_eth_fw_version;
    cluster_desc->fw_bundle_version = first_fw_bundle_version;

    for (auto& [asic_id, tt_device] : devices) {
        const ChipId chip_id = asic_id_to_chip_id.at(asic_id);
        cluster_desc->all_chips.insert(chip_id);
        cluster_desc->chip_arch.insert({chip_id, tt_device->get_arch()});
        cluster_desc->chip_unique_ids.insert({chip_id, asic_id});
        cluster_desc->ethernet_connections.insert({chip_id, {}});

        const PciDeviceInfo pci_info = tt_device->get_pci_device()->get_device_info();
        cluster_desc->chips_with_mmio.insert({chip_id, tt_device->get_communication_device_id()});
        cluster_desc->chip_to_bus_id.insert({chip_id, pci_info.pci_bus});
        cluster_desc->chip_pci_bdfs.insert({chip_id, pci_info.pci_bdf});
        cluster_desc->chip_pcie_lane_counts.insert({chip_id, tt_device->get_pcie_interface()->get_lane_count()});

        if (is_marked_unhealthy(asic_id)) {
            // Nothing past the PCIe link can be read from a device that did not initialize.
            cluster_desc->unhealthy_devices.push_back(chip_id);
            cluster_desc->noc_translation_enabled.insert({chip_id, false});
            continue;
        }

        const ChipInfo chip_info = tt_device->get_chip_info();
        cluster_desc->noc_translation_enabled.insert({chip_id, chip_info.noc_translation_enabled});
        cluster_desc->harvesting_masks_map.insert({chip_id, chip_info.harvesting_masks});
        cluster_desc->chip_board_type.insert({chip_id, chip_info.board_type});
        cluster_desc->asic_locations.insert({chip_id, chip_info.asic_location});
        cluster_desc->add_chip_to_board(chip_id, chip_info.board_id);

        const std::set<uint32_t>& active_channels = active_eth_channels_per_device[asic_id];
        std::set<uint32_t>& idle_channels = cluster_desc->idle_eth_channels[chip_id];
        for (uint32_t channel = 0; channel < tt_device->get_soc_descriptor().get_num_eth_channels(); channel++) {
            if (active_channels.count(channel) == 0) {
                idle_channels.insert(channel);
            }
        }
        cluster_desc->active_eth_channels[chip_id] = active_channels;
    }

    for (const auto& [local, remote] : ethernet_connections) {
        const ChipId local_chip = asic_id_to_chip_id.at(local.first);
        const ChipId remote_chip = asic_id_to_chip_id.at(remote.first);
        cluster_desc->ethernet_connections[local_chip][local.second] = {remote_chip, remote.second};
        cluster_desc->ethernet_connections[remote_chip][remote.second] = {local_chip, local.second};
    }
    for (const auto& [local, remote] : ethernet_connections_to_remote_devices) {
        cluster_desc->ethernet_connections_to_remote_devices[asic_id_to_chip_id.at(local.first)][local.second] = {
            remote.first, remote.second};
    }
    for (const auto& [asic_id, errors] : health_errors) {
        cluster_desc->health_errors[asic_id_to_chip_id.at(asic_id)] = errors;
    }

    cluster_desc->fill_chips_grouped_by_closest_mmio();
    cluster_desc->verify_cluster_descriptor_info();
    return cluster_desc;
}

void TopologyDiscovery::wait_eth_cores_training(TTDevice* tt_device, std::chrono::milliseconds timeout_ms) {
    const SocDescriptor& soc_descriptor = tt_device->get_soc_descriptor();
    for (uint32_t channel = 0; channel < soc_descriptor.get_num_eth_channels(); channel++) {
        const CoreCoord eth_core = soc_descriptor.get_eth_core_for_channel(channel, CoordSystem::NOC0);
        timeout_ms -= tt_device->wait_eth_core_training(eth_core, timeout_ms);
    }
}

bool TopologyDiscovery::is_board_id_included(uint64_t board_id) const { return board_ids.count(board_id) != 0; }

bool TopologyDiscovery::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    const uint32_t first_heartbeat = get_eth_heartbeat(tt_device, eth_core);
    const bool running = utils::poll_until(
        [&]() { return get_eth_heartbeat(tt_device, eth_core) != first_heartbeat; },
        timeout::ETH_HEARTBEAT_TIMEOUT,
        timeout::FIRMWARE_BUSY_POLL_WINDOW,
        timeout::FIRMWARE_POLL_INTERVAL);
    if (!running) {
        report_discovery_issue(
            options.eth_fw_heartbeat_failure,
            error::EthFirmwareHeartbeatError(*tt_device, asic_id, first_heartbeat, eth_core, get_selected_noc_id()),
            asic_id,
            health_errors);
    }
    return running;
}

uint64_t TopologyDiscovery::get_asic_id(TTDevice* tt_device) { return get_unconnected_device_id(tt_device); }

TTDevice* TopologyDiscovery::get_tt_device(const uint64_t asic_id) {
    for (auto* device_map : {&devices, &devices_to_discover}) {
        auto it = device_map->find(asic_id);
        if (it != device_map->end()) {
            return it->second.get();
        }
    }
    return nullptr;
}

bool TopologyDiscovery::is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core) {
    return tt_device->read_eth_core_training_status(eth_core) == EthTrainingStatus::SUCCESS;
}

void TopologyDiscovery::patch_eth_connections() {}

bool TopologyDiscovery::verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    const SemVer eth_fw_version = get_eth_fw_version(tt_device, eth_core);
    // Taken from the first core whose heartbeat runs, so a crashed core never sets the reference.
    if (!expected_eth_fw_version.has_value()) {
        expected_eth_fw_version = eth_fw_version;
        return true;
    }
    if (eth_fw_version == expected_eth_fw_version.value()) {
        return true;
    }
    report_discovery_issue(
        options.eth_fw_mismatch_action,
        error::EthFirmwareMismatchError(
            *tt_device, asic_id, expected_eth_fw_version.value(), eth_fw_version, eth_core, get_selected_noc_id()),
        asic_id,
        health_errors);
    return false;
}

void TopologyDiscovery::verify_fw_bundle_version(TTDevice* tt_device, uint64_t asic_id) {
    const FirmwareBundleVersion fw_bundle_version = tt_device->get_firmware_version();
    const FirmwareBundleVersion minimum_version = get_minimum_compatible_firmware_version(tt_device->get_arch());
    if (fw_bundle_version < minimum_version) {
        report_discovery_issue(
            options.cmfw_unsupported_action,
            error::UnsupportedCMFWError(*tt_device, asic_id, fw_bundle_version, minimum_version),
            asic_id,
            health_errors);
    }

    if (!first_fw_bundle_version.has_value()) {
        first_fw_bundle_version = fw_bundle_version;
    } else if (fw_bundle_version != first_fw_bundle_version.value()) {
        report_discovery_issue(
            options.cmfw_mismatch_action,
            error::CMFWMismatchError(*tt_device, asic_id, first_fw_bundle_version.value(), fw_bundle_version),
            asic_id,
            health_errors);
    }
}

bool TopologyDiscovery::init_device(TTDevice* tt_device, ChipId chip_id, std::chrono::milliseconds timeout) {
    try {
        tt_device->init_tt_device(timeout);
        return true;
    } catch (const error::UmdBaseException& exception) {
        std::optional<ClusterDescriptor::DeviceHealthError> health_error;
        if (const auto* startup = dynamic_cast<const error::UmdException<error::FirmwareStartupError>*>(&exception)) {
            health_error = startup->error();
        } else if (const auto* noc_hang = dynamic_cast<const error::UmdException<error::NocHangError>*>(&exception)) {
            health_error = noc_hang->error();
        } else if (const auto* pcie_hang = dynamic_cast<const error::UmdException<error::PcieHangError>*>(&exception)) {
            health_error = pcie_hang->error();
        }
        // Only device health problems can be ignored; anything else is a bug or a misuse and always propagates.
        if (!health_error.has_value() ||
            options.device_init_failure_action == TopologyDiscoveryOptions::Action::THROW) {
            throw;
        }
        log_warning(
            LogUMD, "Device {} failed to initialize and is kept as unhealthy: {}", chip_id, exception.message());
        health_errors[generate_unhealthy_asic_id(chip_id)].push_back(health_error.value());
        return false;
    }
}

void TopologyDiscovery::add_local_device(std::unique_ptr<TTDevice> tt_device, int device_id) {
    const ChipId chip_id = get_next_chip_id();
    if (!init_device(tt_device.get(), chip_id, timeout::ARC_STARTUP_TIMEOUT)) {
        const uint64_t unhealthy_asic_id = generate_unhealthy_asic_id(chip_id);
        asic_id_to_chip_id[unhealthy_asic_id] = chip_id;
        devices_to_discover.emplace(unhealthy_asic_id, std::move(tt_device));
        return;
    }
    // The KMD hands the vote to the firmware, so it can only be cast once the firmware is up.
    if (!options.low_power) {
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
    }

    const uint64_t asic_id = get_asic_id(tt_device.get());
    log_debug(LogUMD, "Local device {} is chip {} with ASIC id {:#x}.", device_id, chip_id, asic_id);
    if (!first_fw_bundle_version.has_value()) {
        init_first_device(tt_device.get());
    }
    verify_fw_bundle_version(tt_device.get(), asic_id);
    if (options.wait_on_ethernet_link_training) {
        wait_eth_cores_training(tt_device.get());
    }
    board_ids.insert(tt_device->get_board_id());
    asic_id_to_chip_id[asic_id] = chip_id;
    devices_to_discover.emplace(asic_id, std::move(tt_device));
}

}  // namespace tt::umd
