// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <variant>
#include <vector>

#include "common/unsupported.hpp"
#include "common/utils.hpp"
#include "topology/topology_discovery_blackhole.hpp"
#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/types/eth_training_status.hpp"

namespace tt::umd {

namespace {

// The health error a failed device initialization threw, if it is one a cluster descriptor can record.
template <typename ERROR_T>
bool extract_health_error(
    const std::exception_ptr& failure, std::optional<ClusterDescriptor::DeviceHealthError>& health_error) {
    try {
        std::rethrow_exception(failure);
    } catch (const error::UmdException<ERROR_T>& e) {
        health_error = e.error();
        return true;
    } catch (...) {
        return false;
    }
}

template <typename... ERRORS_T>
std::optional<ClusterDescriptor::DeviceHealthError> to_health_error(
    const std::exception_ptr& failure, const std::variant<ERRORS_T...>* /*error_types*/) {
    std::optional<ClusterDescriptor::DeviceHealthError> health_error;
    (extract_health_error<ERRORS_T>(failure, health_error) || ...);
    return health_error;
}

}  // namespace

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    if (options.simulation.has_value()) {
        UMD_NOT_SUPPORTED("Discovering a simulator's chips");
    }

    std::unique_ptr<TopologyDiscovery> topology_discovery =
        create_topology_discovery(options, io_device_type, soc_descriptor_path);
    if (topology_discovery == nullptr) {
        // No device to discover: an empty cluster, which callers on a machine without cards rely on.
        auto cluster_desc = std::make_unique<ClusterDescriptor>();
        cluster_desc->io_device_type = io_device_type;
        cluster_desc->cluster_id = utils::resolve_cluster_id(options.cluster_id);
        return {std::move(cluster_desc), std::map<ChipId, std::unique_ptr<TTDevice>>{}};
    }

    std::unique_ptr<ClusterDescriptor> cluster_desc = topology_discovery->create_ethernet_map();

    std::map<ChipId, std::unique_ptr<TTDevice>> chip_devices;
    for (auto& [asic_id, tt_device] : topology_discovery->devices) {
        chip_devices.emplace(topology_discovery->asic_id_to_chip_id.at(asic_id), std::move(tt_device));
    }
    return {std::move(cluster_desc), std::move(chip_devices)};
}

TopologyDiscovery::TopologyDiscovery(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) :
    options(options), io_device_type(io_device_type), soc_arch_descriptor_(std::move(soc_arch_descriptor)) {}

std::unique_ptr<TopologyDiscovery> TopologyDiscovery::create_topology_discovery(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    if (io_device_type != IODeviceType::PCIe) {
        UMD_NOT_SUPPORTED("Topology discovery over " + DeviceTypeToString.at(io_device_type));
    }

    const std::map<int, PciDeviceInfo> pci_devices = PCIDevice::enumerate_devices_info();
    if (pci_devices.empty()) {
        return nullptr;
    }

    // A cluster holds chips of one architecture, so the first device decides which discovery runs.
    const tt::ARCH arch = pci_devices.begin()->second.get_arch();
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            break;
        case tt::ARCH::WORMHOLE_B0:
        case tt::ARCH::QUASAR:
            UMD_NOT_SUPPORTED("Topology discovery of " + arch_to_str(arch) + " chips");
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("PCIe device {} has no known architecture.", pci_devices.begin()->first));
    }

    auto soc_arch_descriptor = soc_descriptor_path.empty() ? std::make_shared<SocArchDescriptor>(arch)
                                                           : std::make_shared<SocArchDescriptor>(soc_descriptor_path);
    return std::make_unique<TopologyDiscoveryBlackhole>(std::move(soc_arch_descriptor), options, io_device_type);
}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    get_connected_devices();
    if (is_running_on_6u && options.perform_6u_eth_retrain) {
        retrain_eth_cores();
    }
    discover_remote_devices();
    if (options.discover_remote_devices) {
        tunnel_remote_devices_over_all_gateway_links();
    }
    patch_eth_connections();
    return fill_cluster_descriptor_info();
}

void TopologyDiscovery::get_connected_devices() {
    // enumerate_devices() applies TT_VISIBLE_DEVICES and orders devices by BDF, which keeps chip ids stable across
    // resets.
    for (const int device_id : PCIDevice::enumerate_devices()) {
        log_debug(LogUMD, "Opening PCIe device {} for topology discovery.", device_id);
        add_local_device(
            TTDevice::create(device_id, io_device_type, options.use_safe_api, soc_arch_descriptor_), device_id);
    }
}

void TopologyDiscovery::add_local_device(std::unique_ptr<TTDevice> tt_device, int device_id) {
    const ChipId chip_id = get_next_chip_id();
    if (!init_device(tt_device.get(), chip_id, timeout::ARC_STARTUP_TIMEOUT)) {
        // Nothing beyond its failure can be read from the device, so it skips every other check.
        const uint64_t unhealthy_asic_id = generate_unhealthy_asic_id(chip_id);
        log_warning(LogUMD, "Device {} failed to initialize and is marked unhealthy as chip {}.", device_id, chip_id);
        asic_id_to_chip_id.emplace(unhealthy_asic_id, chip_id);
        devices.emplace(unhealthy_asic_id, std::move(tt_device));
        return;
    }

    if (!first_fw_bundle_version.has_value()) {
        init_first_device(tt_device.get());
    }
    const uint64_t asic_id = get_asic_id(tt_device.get());
    verify_fw_bundle_version(tt_device.get(), asic_id);

    if (!options.low_power) {
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
    }
    if (options.wait_on_ethernet_link_training) {
        wait_eth_cores_training(tt_device.get());
    }

    board_ids.insert(tt_device->get_board_id());
    log_debug(LogUMD, "Device {} is chip {} with ASIC id {:#x}.", device_id, chip_id, asic_id);
    asic_id_to_chip_id.emplace(asic_id, chip_id);
    devices_to_discover.emplace(asic_id, std::move(tt_device));
}

bool TopologyDiscovery::init_device(TTDevice* tt_device, ChipId chip_id, std::chrono::milliseconds timeout) {
    try {
        tt_device->init_tt_device(timeout);
        return true;
    } catch (const std::exception& e) {
        if (options.device_init_failure_action == TopologyDiscoveryOptions::Action::THROW) {
            throw;
        }
        log_error(LogUMD, "Chip {} failed to initialize: {}", chip_id, e.what());
        const std::optional<ClusterDescriptor::DeviceHealthError> health_error = to_health_error(
            std::current_exception(), static_cast<const ClusterDescriptor::DeviceHealthError*>(nullptr));
        if (health_error.has_value()) {
            health_errors[generate_unhealthy_asic_id(chip_id)].push_back(*health_error);
        }
        return false;
    }
}

void TopologyDiscovery::discover_remote_devices() {
    // Walks the ETH links of every local device. The far end of a link is either another local device or a chip
    // this process does not open, which is recorded by its ASIC id.
    while (!devices_to_discover.empty()) {
        auto device_node = devices_to_discover.extract(devices_to_discover.begin());
        const uint64_t asic_id = device_node.key();
        TTDevice* tt_device = device_node.mapped().get();
        devices.insert(std::move(device_node));

        const SocDescriptor& soc_descriptor = tt_device->get_soc_descriptor();
        for (const CoreCoord& eth_core : soc_descriptor.get_cores(CoreType::ETH, CoordSystem::NOC0)) {
            verify_eth_core_fw_version(tt_device, asic_id, eth_core);
            if (is_eth_port_disabled(tt_device, eth_core) || !is_eth_trained(tt_device, eth_core) ||
                !eth_heartbeat_running(tt_device, asic_id, eth_core)) {
                continue;
            }
            verify_routing_firmware_state(tt_device, asic_id, eth_core);

            const uint32_t channel = soc_descriptor.get_eth_channel_for_core(eth_core, CoordSystem::NOC0);
            active_eth_channels_per_device[asic_id].insert(channel);

            const uint64_t remote_asic_id = get_remote_asic_id(tt_device, eth_core);
            if (asic_id_to_chip_id.count(remote_asic_id) != 0) {
                ethernet_connections.push_back(
                    {{asic_id, channel}, {remote_asic_id, get_remote_eth_channel(tt_device, eth_core)}});
            } else {
                ethernet_connections_to_remote_devices.push_back(
                    {{asic_id, channel}, {remote_asic_id, get_logical_remote_eth_channel(tt_device, eth_core)}});
            }
        }
    }
}

void TopologyDiscovery::tunnel_remote_devices_over_all_gateway_links() {
    if (!remote_asic_id_to_mmio_device_id.empty()) {
        UMD_NOT_SUPPORTED("Tunnelling to remote devices");
    }
}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::fill_cluster_descriptor_info() {
    auto cluster_desc = std::make_unique<ClusterDescriptor>();
    cluster_desc->io_device_type = io_device_type;
    cluster_desc->cluster_id = utils::resolve_cluster_id(options.cluster_id);
    cluster_desc->fw_bundle_version = first_fw_bundle_version;
    cluster_desc->eth_fw_version = expected_eth_fw_version;
    // Discovery reads every ASIC id from its chip, so unique ids identify chips across processes.
    cluster_desc->authentic_chip_unique_ids = true;

    for (const auto& [asic_id, tt_device] : devices) {
        const ChipId chip_id = asic_id_to_chip_id.at(asic_id);
        cluster_desc->all_chips.insert(chip_id);
        cluster_desc->chip_arch.emplace(chip_id, get_topology_arch());
        cluster_desc->chip_unique_ids.emplace(chip_id, asic_id);
        // Every chip has an entry, so lookups by chip work for chips without links.
        cluster_desc->ethernet_connections[chip_id];
        if (!tt_device->is_remote()) {
            cluster_desc->chips_with_mmio.emplace(chip_id, tt_device->get_communication_device_id());
            const PciDeviceInfo pci_info = tt_device->get_pci_device()->get_device_info();
            cluster_desc->chip_to_bus_id.emplace(chip_id, pci_info.pci_bus);
            cluster_desc->chip_pci_bdfs.emplace(chip_id, pci_info.pci_bdf);
            cluster_desc->chip_pcie_lane_counts.emplace(chip_id, tt_device->get_pcie_interface()->get_lane_count());
        }

        if (is_marked_unhealthy(asic_id)) {
            // An uninitialized device has no board, harvesting or ETH information to report.
            cluster_desc->unhealthy_devices.push_back(chip_id);
            cluster_desc->chip_board_type.emplace(chip_id, BoardType::UNKNOWN);
            cluster_desc->noc_translation_enabled.emplace(chip_id, false);
            continue;
        }

        const SocDescriptor& soc_descriptor = tt_device->get_soc_descriptor();
        cluster_desc->chip_board_type.emplace(chip_id, tt_device->get_board_type());
        cluster_desc->add_chip_to_board(chip_id, tt_device->get_board_id());
        cluster_desc->asic_locations.emplace(chip_id, tt_device->get_asic_location());
        cluster_desc->noc_translation_enabled.emplace(chip_id, soc_descriptor.noc_translation_enabled);
        cluster_desc->harvesting_masks_map.emplace(chip_id, soc_descriptor.harvesting_masks);
        if (is_using_eth_coords() && eth_coords.count(asic_id) != 0) {
            cluster_desc->chip_locations.emplace(chip_id, eth_coords.at(asic_id));
        }

        const std::set<uint32_t>& active_channels = active_eth_channels_per_device[asic_id];
        const uint32_t num_eth_channels = soc_descriptor.get_cores(CoreType::ETH).size();
        for (uint32_t channel = 0; channel < num_eth_channels; channel++) {
            if (active_channels.count(channel) != 0) {
                cluster_desc->active_eth_channels[chip_id].insert(channel);
            } else {
                cluster_desc->idle_eth_channels[chip_id].insert(channel);
            }
        }
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
        auto& chip_errors = cluster_desc->health_errors[asic_id_to_chip_id.at(asic_id)];
        chip_errors.insert(chip_errors.end(), errors.begin(), errors.end());
    }

    cluster_desc->merge_cluster_ids();
    cluster_desc->fill_chips_grouped_by_closest_mmio();
    cluster_desc->verify_cluster_descriptor_info();
    return cluster_desc;
}

void TopologyDiscovery::wait_eth_cores_training(TTDevice* tt_device, std::chrono::milliseconds timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout_ms;
    for (const CoreCoord& eth_core : tt_device->get_soc_descriptor().get_cores(CoreType::ETH)) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        tt_device->wait_eth_core_training(eth_core, std::max(remaining, std::chrono::milliseconds(0)));
    }
}

bool TopologyDiscovery::is_board_id_included(uint64_t board_id) const { return board_ids.count(board_id) != 0; }

bool TopologyDiscovery::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    const uint32_t first_heartbeat = get_eth_heartbeat(tt_device, eth_core);
    uint32_t heartbeat = first_heartbeat;
    const bool running = utils::poll_until(
        [&]() {
            heartbeat = get_eth_heartbeat(tt_device, eth_core);
            return heartbeat != first_heartbeat;
        },
        timeout::ETH_HEARTBEAT_TIMEOUT,
        timeout::FIRMWARE_BUSY_POLL_WINDOW,
        timeout::FIRMWARE_POLL_INTERVAL);
    if (running) {
        return true;
    }

    if (options.eth_fw_heartbeat_failure == TopologyDiscoveryOptions::Action::THROW) {
        UMD_THROW(error::EthFirmwareHeartbeatError, *tt_device, asic_id, heartbeat, eth_core);
    }
    log_warning(
        LogUMD,
        "ETH firmware heartbeat on core {} of ASIC {:#x} is not advancing (postcode {:#x}); skipping the core.",
        eth_core.str(),
        asic_id,
        get_eth_postcode(tt_device, eth_core));
    health_errors[asic_id].push_back(error::EthFirmwareHeartbeatError(*tt_device, asic_id, heartbeat, eth_core));
    return false;
}

uint64_t TopologyDiscovery::get_asic_id(TTDevice* tt_device) {
    for (const CoreCoord& eth_core : tt_device->get_soc_descriptor().get_cores(CoreType::ETH)) {
        if (is_eth_trained(tt_device, eth_core)) {
            return get_local_asic_id(tt_device, eth_core);
        }
    }
    return get_unconnected_device_id(tt_device);
}

TTDevice* TopologyDiscovery::get_tt_device(const uint64_t asic_id) {
    for (const auto* device_map : {&devices, &devices_to_discover}) {
        auto it = device_map->find(asic_id);
        if (it != device_map->end()) {
            return it->second.get();
        }
    }
    UMD_THROW(error::RuntimeError, fmt::format("No discovered device has ASIC id {:#x}.", asic_id));
}

bool TopologyDiscovery::is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core) {
    return tt_device->read_eth_core_training_status(eth_core) == EthTrainingStatus::SUCCESS;
}

// The ETH channel the cores report ignores harvesting only on Blackhole, which overrides this.
void TopologyDiscovery::patch_eth_connections() {}

bool TopologyDiscovery::verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    const SemVer eth_fw_version = get_eth_fw_version(tt_device, eth_core);
    if (!expected_eth_fw_version.has_value()) {
        log_debug(LogUMD, "Expecting ETH firmware {} on every ETH core.", eth_fw_version.str());
        expected_eth_fw_version = eth_fw_version;
        return true;
    }
    if (eth_fw_version == *expected_eth_fw_version) {
        return true;
    }

    if (options.eth_fw_mismatch_action == TopologyDiscoveryOptions::Action::THROW) {
        UMD_THROW(
            error::EthFirmwareMismatchError, *tt_device, asic_id, *expected_eth_fw_version, eth_fw_version, eth_core);
    }
    log_warning(
        LogUMD,
        "ETH firmware {} on core {} of ASIC {:#x} differs from the expected {}.",
        eth_fw_version.str(),
        eth_core.str(),
        asic_id,
        expected_eth_fw_version->str());
    health_errors[asic_id].push_back(
        error::EthFirmwareMismatchError(*tt_device, asic_id, *expected_eth_fw_version, eth_fw_version, eth_core));
    return false;
}

void TopologyDiscovery::verify_fw_bundle_version(TTDevice* tt_device, uint64_t asic_id) {
    const FirmwareBundleVersion fw_bundle_version = tt_device->get_firmware_version();
    const tt::ARCH arch = tt_device->get_arch();

    const FirmwareBundleVersion minimum_version = get_minimum_compatible_firmware_version(arch);
    if (fw_bundle_version < minimum_version) {
        if (options.cmfw_unsupported_action == TopologyDiscoveryOptions::Action::THROW) {
            UMD_THROW(error::UnsupportedCMFWError, *tt_device, asic_id, fw_bundle_version, minimum_version);
        }
        log_warning(
            LogUMD,
            "Firmware bundle {} on ASIC {:#x} is older than the minimum supported {}.",
            fw_bundle_version.str(),
            asic_id,
            minimum_version.str());
        health_errors[asic_id].push_back(
            error::UnsupportedCMFWError(*tt_device, asic_id, fw_bundle_version, minimum_version));
    }

    const FirmwareBundleVersion latest_version = get_latest_supported_firmware_version(arch);
    if (fw_bundle_version > latest_version) {
        log_warning(
            LogUMD,
            "Firmware bundle {} on ASIC {:#x} is newer than the latest version UMD was tested with, {}.",
            fw_bundle_version.str(),
            asic_id,
            latest_version.str());
    }

    if (!first_fw_bundle_version.has_value()) {
        first_fw_bundle_version = fw_bundle_version;
        return;
    }
    if (fw_bundle_version == *first_fw_bundle_version) {
        return;
    }
    if (options.cmfw_mismatch_action == TopologyDiscoveryOptions::Action::THROW) {
        UMD_THROW(error::CMFWMismatchError, *tt_device, asic_id, *first_fw_bundle_version, fw_bundle_version);
    }
    log_warning(
        LogUMD,
        "Firmware bundle {} on ASIC {:#x} differs from {} found on the other devices.",
        fw_bundle_version.str(),
        asic_id,
        first_fw_bundle_version->str());
    health_errors[asic_id].push_back(
        error::CMFWMismatchError(*tt_device, asic_id, *first_fw_bundle_version, fw_bundle_version));
}

}  // namespace tt::umd
