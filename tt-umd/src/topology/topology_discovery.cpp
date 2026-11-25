// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cluster_description_yaml.hpp"
#include "common/utils.hpp"
#include "firmware/erisc_firmware.hpp"
#include "simulation/simulation_backend.hpp"
#include "simulation/simulator_paths.hpp"
#include "topology/topology_discovery_base.hpp"
#include "topology/topology_discovery_blackhole.hpp"
#include "topology/topology_discovery_wormhole.hpp"
#include "topology/topology_utils.hpp"
#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/cluster_description.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/topology_discovery_error.hpp"
#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/remote_communication.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/semver.hpp"
#include "tt-umd/utils/timeouts.hpp"
#include "utils.hpp"

namespace tt::umd {

namespace {

// Probe the architecture from the host bus. Returns ARCH::Invalid when the bus has no devices.
tt::ARCH probe_bus_architecture(IODeviceType io_device_type) {
    switch (io_device_type) {
        case IODeviceType::PCIe: {
            auto pci_devices_info = PCIDevice::enumerate_devices_info();
            if (pci_devices_info.empty()) {
                return ARCH::Invalid;
            }
            return pci_devices_info.begin()->second.get_arch();
        }
        default:
            UMD_THROW(error::RuntimeError, "Unsupported device type for topology discovery.");
    }
}

// The SoC descriptor file discovery builds its devices from: the caller's, else the one a simulator ships beside it.
// Empty selects the architecture's built-in descriptor.
std::string resolve_soc_descriptor_path(const TopologyDiscoveryOptions& options) {
    if (!options.soc_descriptor_path.empty() || !options.simulation.has_value()) {
        return options.soc_descriptor_path;
    }
    return find_simulator_soc_descriptor_path(options.simulation->simulator_path);
}

std::shared_ptr<SocArchDescriptor> create_soc_arch_descriptor(
    const std::string& soc_descriptor_path, tt::ARCH device_arch) {
    if (soc_descriptor_path.empty()) {
        return std::make_shared<SocArchDescriptor>(device_arch);
    }
    auto soc_arch_descriptor = std::make_shared<SocArchDescriptor>(soc_descriptor_path);
    UMD_ASSERT(
        soc_arch_descriptor->get_arch() == device_arch,
        error::RuntimeError,
        fmt::format(
            "Architecture {} in SocArchDescriptor file on path {} does not match the device architecture {}.",
            arch_to_str(soc_arch_descriptor->get_arch()),
            soc_descriptor_path,
            arch_to_str(device_arch)));
    return soc_arch_descriptor;
}

// A chip's description as a SocDescriptor wants it: what the declared topology says about the chip, until the device's
// firmware reports it.
ChipInfo get_declared_chip_info(const ChipDescription& chip) {
    ChipInfo chip_info;
    chip_info.noc_translation_enabled = chip.noc_translation_enabled;
    chip_info.harvesting_masks = chip.harvesting_masks;
    chip_info.board_type = chip.board_type;
    chip_info.asic_location = chip.asic_location.value_or(0);
    return chip_info;
}

// Leaves out the chips the host reaches only over Ethernet, and the links to them, as discovery does when it does not
// follow Ethernet links. The channels of those links stay active on the chips that are kept, since they still carry
// traffic to chips outside the cluster.
void drop_remote_chips(ClusterDescription& description) {
    for (auto it = description.chips.begin(); it != description.chips.end();) {
        it = it->second.mmio_device_id.has_value() ? std::next(it) : description.chips.erase(it);
    }
    const auto links_a_dropped_chip = [&description](const EthLink& link) {
        return description.chips.count(link.chip_a) == 0 || description.chips.count(link.chip_b) == 0;
    };
    description.eth_links.erase(
        std::remove_if(description.eth_links.begin(), description.eth_links.end(), links_a_dropped_chip),
        description.eth_links.end());
}

using DiscoveredTopology = std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>>;

// A chip's description as a SocDescriptor wants it, from a topology that describes it without its device's firmware.
ChipInfo get_described_chip_info(const ClusterDescriptor& topology, ChipId chip_id) {
    ChipInfo chip_info;
    chip_info.noc_translation_enabled = topology.get_noc_translation_table_en().at(chip_id);
    chip_info.harvesting_masks = topology.get_harvesting_masks(chip_id);
    chip_info.board_type = topology.get_board_type(chip_id);
    chip_info.asic_location = topology.get_asic_location(chip_id);
    return chip_info;
}

// The chips of a simulator's whole topology that this process discovers: the caller's target_devices, else the chips
// TT_VISIBLE_DEVICES names, a chip of a two-chip board bringing in the whole board. Chips left out are dropped and the
// rest renumbered from 0, MMIO-capable chips first, as a Cluster numbers a subset of its chips.
std::unique_ptr<ClusterDescriptor> select_simulated_chips(
    const ClusterDescriptor& topology, const SimulationDiscoveryOptions& simulation) {
    for (const ChipId chip_id : simulation.target_devices) {
        UMD_ASSERT(
            topology.get_all_chips().count(chip_id) != 0,
            error::RuntimeError,
            fmt::format(
                "Target device {} is not one of the {} chip(s) of simulator {}.",
                chip_id,
                topology.get_number_of_chips(),
                simulation.simulator_path));
    }
    return ClusterDescriptor::create_constrained_cluster_descriptor(&topology, simulation.target_devices);
}

// For each chip of selection, a part of topology that select_simulated_chips or create_constrained_cluster_descriptor
// renumbered, the id topology gives the same chip. Chips are matched by unique id, which renumbering keeps.
std::map<ChipId, ChipId> get_topology_chip_ids(const ClusterDescriptor& topology, const ClusterDescriptor& selection) {
    std::unordered_map<uint64_t, ChipId> chip_by_unique_id;
    for (const auto& [chip_id, unique_id] : topology.get_chip_unique_ids()) {
        const auto [it, inserted] = chip_by_unique_id.emplace(unique_id, chip_id);
        UMD_ASSERT(
            inserted,
            error::RuntimeError,
            fmt::format(
                "Chips {} and {} of the simulator's topology share unique id {}, so a selection of its chips cannot be "
                "told apart.",
                it->second,
                chip_id,
                unique_id));
    }
    std::map<ChipId, ChipId> topology_chip_ids;
    for (const auto& [chip_id, unique_id] : selection.get_chip_unique_ids()) {
        topology_chip_ids.emplace(chip_id, chip_by_unique_id.at(unique_id));
    }
    return topology_chip_ids;
}

// The devices opened for chips of topology, keyed instead by the ids selection gives them. Devices of chips the
// selection leaves out are closed.
std::map<ChipId, std::unique_ptr<TTDevice>> rekey_to_selection(
    std::map<ChipId, std::unique_ptr<TTDevice>> devices,
    const ClusterDescriptor& topology,
    const ClusterDescriptor& selection) {
    std::map<ChipId, std::unique_ptr<TTDevice>> selected_devices;
    for (const auto& [chip_id, topology_chip_id] : get_topology_chip_ids(topology, selection)) {
        auto it = devices.find(topology_chip_id);
        if (it != devices.end()) {
            selected_devices.emplace(chip_id, std::move(it->second));
        }
    }
    return selected_devices;
}

// A topology whose devices are all open, narrowed to the chips this process discovers. A topology selected whole is
// kept as it is, since narrowing would also drop the unhealthy chips it reports.
DiscoveredTopology select_opened_simulated_chips(
    DiscoveredTopology topology, const SimulationDiscoveryOptions& simulation) {
    auto& [cluster_desc, devices] = topology;
    std::unique_ptr<ClusterDescriptor> selection = select_simulated_chips(*cluster_desc, simulation);
    if (selection->get_number_of_chips() == cluster_desc->get_number_of_chips()) {
        return topology;
    }
    devices = rekey_to_selection(std::move(devices), *cluster_desc, *selection);
    cluster_desc = std::move(selection);
    return topology;
}

// A cluster_descriptor.yaml beside the simulator declares its topology, so there is nothing to walk: the descriptor is
// the declared one, with a device for each of its chips this process discovers. The descriptor is written by hand
// rather than read from the image, so each chip's architecture is checked against the image the chip runs on. As for a
// walked topology, the caller's cluster id is stamped, remote chips are left out when the caller asks for local ones
// only, and every device is initialized. The walk's firmware-version and Ethernet checks are not run: the declaration,
// not a walk, states the links.
DiscoveredTopology discover_declared_simulation_topology(
    const TopologyDiscoveryOptions& options, const std::filesystem::path& cluster_descriptor_path) {
    const std::filesystem::path simulator_path = options.simulation->simulator_path;
    ClusterDescription description = load_cluster_description_yaml(cluster_descriptor_path);

    const tt::ARCH simulator_arch = simulation_backend().get_simulator_arch(simulator_path);
    for (const auto& [chip_id, chip] : description.chips) {
        UMD_ASSERT(
            chip.arch == simulator_arch,
            error::RuntimeError,
            fmt::format(
                "Cluster descriptor gives chip {} architecture {}, but simulator {} is {}.",
                chip_id,
                arch_to_str(chip.arch),
                simulator_path.string(),
                arch_to_str(simulator_arch)));
    }
    description.cluster_id = utils::resolve_cluster_id(options.cluster_id);
    if (!options.discover_remote_devices) {
        drop_remote_chips(description);
    }
    // A simulator's harvesting is whatever its image models and need not match its board type.
    std::unique_ptr<ClusterDescriptor> cluster_desc =
        create_cluster_descriptor(description, options.discover_remote_devices, /*check_harvesting_counts=*/false);

    // Devices are opened only for the selected chips, so that other processes can open the rest. They are keyed by
    // the declared chip ids until the final descriptor is known, as a chip that fails to initialize changes it.
    std::unique_ptr<ClusterDescriptor> selection = select_simulated_chips(*cluster_desc, *options.simulation);
    const std::map<ChipId, ChipId> declared_chip_ids = get_topology_chip_ids(*cluster_desc, *selection);
    const bool selected_whole = selection->get_number_of_chips() == cluster_desc->get_number_of_chips();

    const std::shared_ptr<SocArchDescriptor> soc_arch_descriptor =
        create_soc_arch_descriptor(resolve_soc_descriptor_path(options), simulator_arch);
    const int num_host_mem_channels = options.simulation->num_host_mem_channels.value_or(
        static_cast<int>(get_host_mem_channels_for_topology(*selection)));

    // A device that fails to initialize is kept, and its chip reported unhealthy, unless the caller asked to throw.
    std::vector<ChipId> unhealthy_chips;
    auto init_device = [&](TTDevice& device, ChipId chip_id, std::chrono::milliseconds timeout) {
        try {
            device.init_tt_device(timeout);
        } catch (error::UmdBaseException& err) {
            if (options.device_init_failure_action == TopologyDiscoveryOptions::Action::THROW) {
                throw;
            }
            log_warning(LogUMD, err.message());
            if (std::optional<ClusterDescriptor::DeviceHealthError> health_error = determine_device_init_error(err)) {
                description.health_errors[chip_id].push_back(std::move(*health_error));
            }
            unhealthy_chips.push_back(chip_id);
        }
    };

    // MMIO-capable chips come first, as every remote chip is reached through one of them. A chip's endpoint on the
    // image is its declared id, so the image is opened as the whole declaration's MMIO-capable chips.
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    const size_t num_mmio_chips = cluster_desc->get_chips_with_mmio().size();
    for (const ChipId selected_id : selection->get_chips_local_first(selection->get_all_chips())) {
        const ChipId chip_id = declared_chip_ids.at(selected_id);
        std::unique_ptr<TTDevice> device;
        if (!selection->is_chip_remote(selected_id)) {
            // A simulated device sizes its system memory as it is constructed, so it is built from the declared
            // description and rebuilds its SoC descriptor from what its firmware reports once initialized.
            const SocDescriptor soc_descriptor(
                soc_arch_descriptor, get_declared_chip_info(description.chips.at(chip_id)));
            device = simulation_backend().create_simulation_tt_device(
                simulator_path, soc_descriptor, chip_id, num_mmio_chips, num_host_mem_channels);
            init_device(*device, chip_id, timeout::ARC_LONG_POST_RESET_TIMEOUT);
        } else {
            const ChipId gateway_id = declared_chip_ids.at(selection->get_closest_mmio_capable_chip(selected_id));
            // A simulator may end the process rather than throw on traffic through an uninitialized chip, so a chip
            // behind one is not opened.
            if (std::find(unhealthy_chips.begin(), unhealthy_chips.end(), gateway_id) != unhealthy_chips.end()) {
                log_warning(
                    LogUMD,
                    "Chip {} is not opened: its closest MMIO-capable chip {} failed to initialize.",
                    chip_id,
                    gateway_id);
                unhealthy_chips.push_back(chip_id);
                continue;
            }
            TTDevice* gateway_device = devices.at(gateway_id).get();
            std::unique_ptr<RemoteCommunication> remote_communication =
                RemoteCommunication::create_remote_communication(
                    gateway_device, cluster_desc->get_chip_location(chip_id));
            remote_communication->set_remote_transfer_ethernet_cores(
                gateway_device->get_soc_descriptor().get_eth_xy_pairs_for_channels(
                    cluster_desc->get_active_eth_channels(gateway_id), CoordSystem::TRANSLATED));
            device = create_tt_device(std::move(remote_communication), soc_arch_descriptor);
            init_device(*device, chip_id, timeout::ARC_STARTUP_TIMEOUT);
        }
        devices.emplace(chip_id, std::move(device));
    }
    log_debug(
        LogUMD,
        "Opened {} simulated device(s) of the topology declared in {}.",
        devices.size(),
        cluster_descriptor_path.string());

    if (unhealthy_chips.empty()) {
        if (selected_whole) {
            return std::make_pair(std::move(cluster_desc), std::move(devices));
        }
        devices = rekey_to_selection(std::move(devices), *cluster_desc, *selection);
        return std::make_pair(std::move(selection), std::move(devices));
    }
    for (const ChipId chip_id : unhealthy_chips) {
        description.chips.erase(chip_id);
        description.unhealthy_chips.push_back(chip_id);
    }
    if (selected_whole) {
        cluster_desc = create_cluster_descriptor(
            std::move(description), options.discover_remote_devices, /*check_harvesting_counts=*/false);
        return std::make_pair(std::move(cluster_desc), std::move(devices));
    }
    // A selection keeps out the chips it leaves out, and reports the unhealthy ones only through its devices' absence.
    std::unordered_set<ChipId> healthy_selected_chips;
    for (const auto& [selected_id, chip_id] : declared_chip_ids) {
        if (description.chips.count(chip_id) != 0) {
            healthy_selected_chips.insert(chip_id);
        }
    }
    if (healthy_selected_chips.empty()) {
        ClusterDescription no_healthy_chips;
        no_healthy_chips.cluster_id = description.cluster_id;
        no_healthy_chips.unhealthy_chips = std::move(description.unhealthy_chips);
        no_healthy_chips.health_errors = std::move(description.health_errors);
        return std::make_pair(create_cluster_descriptor(std::move(no_healthy_chips)), std::move(devices));
    }
    cluster_desc = create_cluster_descriptor(
        std::move(description), options.discover_remote_devices, /*check_harvesting_counts=*/false);
    std::unique_ptr<ClusterDescriptor> healthy_selection =
        ClusterDescriptor::create_constrained_cluster_descriptor(cluster_desc.get(), healthy_selected_chips);
    devices = rekey_to_selection(std::move(devices), *cluster_desc, *healthy_selection);
    return std::make_pair(std::move(healthy_selection), std::move(devices));
}

// The topology of a simulator whose chips are known only by their count: each is the only chip of its own board and
// has its own endpoint, its unique id is its position, and its board and harvesting are the placeholders of a mock
// cluster. The caller's cluster id is stamped as on any discovered topology.
std::unique_ptr<ClusterDescriptor> create_counted_simulation_topology(
    size_t num_chips, tt::ARCH arch, bool noc_translation_enabled, const TopologyDiscoveryOptions& options) {
    ClusterDescription description;
    description.cluster_id = utils::resolve_cluster_id(options.cluster_id);
    std::unordered_set<ChipId> chip_ids;
    for (size_t position = 0; position < num_chips; ++position) {
        chip_ids.insert(static_cast<ChipId>(position));
    }
    const std::unique_ptr<ClusterDescriptor> placeholders =
        ClusterDescriptor::create_mock_cluster(chip_ids, arch, noc_translation_enabled);
    for (const ChipId chip_id : chip_ids) {
        ChipDescription chip;
        chip.arch = arch;
        chip.board_type = placeholders->get_board_type(chip_id);
        chip.board_id = placeholders->get_board_id_for_chip(chip_id);
        chip.unique_id = static_cast<uint64_t>(chip_id);
        chip.eth_coord = placeholders->get_chip_location(chip_id);
        chip.harvesting_masks = placeholders->get_harvesting_masks(chip_id);
        chip.noc_translation_enabled = noc_translation_enabled;
        chip.mmio_device_id = chip_id;
        chip.pci_bdf = placeholders->get_chip_pci_bdfs().at(chip_id);
        chip.pci_bus_id = placeholders->get_bus_id(chip_id);
        chip.asic_location = placeholders->get_asic_location(chip_id);
        description.chips.emplace(chip_id, std::move(chip));
    }
    // A simulator's harvesting is whatever its image models and need not match its board type.
    return create_cluster_descriptor(
        std::move(description), /*check_board_chip_count=*/true, /*check_harvesting_counts=*/false);
}

// An RTL simulator build with an ip_layout.yaml states its chips: one per device of the layout, all served by one run
// of the simulator. Only the selected devices are opened, so other processes can serve the rest of the layout.
DiscoveredTopology discover_ip_layout_simulation_topology(
    const TopologyDiscoveryOptions& options, const std::vector<std::string>& layout_soc_descriptor_paths) {
    const SimulationDiscoveryOptions& simulation = *options.simulation;
    // The layout's chip ids are the devices' positions in it. RTL simulation runs without NOC translation.
    const std::unique_ptr<ClusterDescriptor> layout_topology = create_counted_simulation_topology(
        layout_soc_descriptor_paths.size(),
        SocDescriptor::get_arch_from_soc_descriptor_path(layout_soc_descriptor_paths.front()),
        /*noc_translation_enabled=*/false,
        options);
    std::unique_ptr<ClusterDescriptor> cluster_desc = select_simulated_chips(*layout_topology, simulation);

    std::map<ChipId, size_t> layout_devices;
    for (const auto& [chip_id, layout_chip_id] : get_topology_chip_ids(*layout_topology, *cluster_desc)) {
        layout_devices.emplace(chip_id, static_cast<size_t>(layout_chip_id));
    }
    const int num_host_mem_channels =
        simulation.num_host_mem_channels.value_or(static_cast<int>(get_host_mem_channels_for_topology(*cluster_desc)));
    std::map<ChipId, std::unique_ptr<TTDevice>> devices = simulation_backend().create_ip_layout_tt_devices(
        simulation.simulator_path, layout_devices, num_host_mem_channels, simulation.launch_simulator);
    log_debug(
        LogUMD,
        "Opened {} of the {} device(s) of the ip_layout.yaml.",
        devices.size(),
        layout_soc_descriptor_paths.size());
    return std::make_pair(std::move(cluster_desc), std::move(devices));
}

// A simulator that neither declares its chips nor can be enumerated models one chip, of the simulator's architecture.
// NOC translation is what its backend runs with: on for TTSim, off for RTL.
DiscoveredTopology discover_one_chip_simulation_topology(
    const TopologyDiscoveryOptions& options, SimulationBackendType backend) {
    const SimulationDiscoveryOptions& simulation = *options.simulation;
    const std::string soc_descriptor_path = resolve_soc_descriptor_path(options);
    // An RTL build tells its architecture only through the SoC descriptor it ships, so one that ships none is taken to
    // be of the caller's descriptor's architecture. Otherwise the caller's descriptor is checked against the simulator.
    const bool arch_from_callers_descriptor = backend == SimulationBackendType::RTL &&
                                              !options.soc_descriptor_path.empty() &&
                                              find_simulator_soc_descriptor_path(simulation.simulator_path).empty();
    const tt::ARCH arch = arch_from_callers_descriptor
                              ? SocDescriptor::get_arch_from_soc_descriptor_path(soc_descriptor_path)
                              : simulation_backend().get_simulator_arch(simulation.simulator_path);
    const std::unique_ptr<ClusterDescriptor> one_chip_topology = create_counted_simulation_topology(
        1, arch, /*noc_translation_enabled=*/backend == SimulationBackendType::TTSIM, options);
    std::unique_ptr<ClusterDescriptor> cluster_desc = select_simulated_chips(*one_chip_topology, simulation);

    constexpr ChipId chip_id = 0;
    const SocDescriptor soc_descriptor(
        create_soc_arch_descriptor(soc_descriptor_path, arch), get_described_chip_info(*cluster_desc, chip_id));
    const int num_host_mem_channels =
        simulation.num_host_mem_channels.value_or(static_cast<int>(get_host_mem_channels_for_topology(*cluster_desc)));
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    devices.emplace(
        chip_id,
        simulation_backend().create_simulation_tt_device(
            simulation.simulator_path, soc_descriptor, chip_id, /*num_chips=*/1, num_host_mem_channels));
    return std::make_pair(std::move(cluster_desc), std::move(devices));
}

// A server directory is where another process hosting a simulator serves its chips, one socket per chip. Its topology
// is the one the server serves, with the caller's cluster id stamped and remote chips left out when the caller asks for
// local ones only. Each chip is driven through the server over its own socket, so every chip this process discovers
// needs a live one.
DiscoveredTopology discover_served_simulation_topology(const TopologyDiscoveryOptions& options) {
    const SimulationDiscoveryOptions& simulation = *options.simulation;
    // Resolved first, so that an illegal cluster id is refused before anything attaches to the server.
    std::optional<std::string> cluster_id = utils::resolve_cluster_id(options.cluster_id);
    SimulationConnectorOptions connector_options;
    connector_options.simulator_directory = simulation.simulator_path;
    SimulationConnector::Result server = SimulationConnector::discover(connector_options);

    ClusterDescription description = parse_cluster_description_yaml(server.cluster_descriptor->serialize());
    description.cluster_id = std::move(cluster_id);
    if (!options.discover_remote_devices) {
        drop_remote_chips(description);
    }
    // A simulator's harvesting is whatever its image models and need not match its board type.
    std::unique_ptr<ClusterDescriptor> served_topology = create_cluster_descriptor(
        std::move(description), options.discover_remote_devices, /*check_harvesting_counts=*/false);
    std::unique_ptr<ClusterDescriptor> selection = select_simulated_chips(*served_topology, simulation);
    // A topology selected whole keeps the server's chip ids, so that each chip has the same id here as on the server.
    const bool selected_whole = selection->get_number_of_chips() == served_topology->get_number_of_chips();
    std::map<ChipId, ChipId> served_chip_ids;
    if (selected_whole) {
        for (const ChipId chip_id : served_topology->get_all_chips()) {
            served_chip_ids.emplace(chip_id, chip_id);
        }
    } else {
        served_chip_ids = get_topology_chip_ids(*served_topology, *selection);
    }
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    for (const auto& [chip_id, served_chip_id] : served_chip_ids) {
        auto it = server.devices.find(served_chip_id);
        UMD_ASSERT(
            it != server.devices.end(),
            error::RuntimeError,
            fmt::format(
                "Chip {} of the topology served in {} has no live simulation socket there.",
                served_chip_id,
                simulation.simulator_path));
        devices.emplace(chip_id, std::move(it->second));
    }
    log_debug(
        LogUMD,
        "Attached to {} of the {} chip(s) served in {}.",
        devices.size(),
        served_topology->get_number_of_chips(),
        simulation.simulator_path);
    return std::make_pair(selected_whole ? std::move(served_topology) : std::move(selection), std::move(devices));
}

}  // namespace

std::unique_ptr<TopologyDiscoveryBase> TopologyDiscoveryBase::create_topology_discovery(
    const TopologyDiscoveryOptions& options) {
    // A simulator's architecture is read from the image itself rather than from the supplied
    // descriptor, which is what gives the cross-check below two things to compare -- a supplied
    // descriptor that disagrees with the image selects no topology at all instead of the subclass
    // of an architecture the devices are not.
    tt::ARCH current_arch = ARCH::Invalid;
    if (!options.simulation.has_value()) {
        current_arch = probe_bus_architecture(options.io_device_type);
    } else {
        current_arch = simulation_backend().get_simulator_arch(options.simulation->simulator_path);
    }
    if (current_arch == ARCH::Invalid) {
        return nullptr;
    }

    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor =
        create_soc_arch_descriptor(resolve_soc_descriptor_path(options), current_arch);

    log_info(LogUMD, "Creating TopologyDiscovery for architecture: {}", arch_to_str(current_arch));
    switch (current_arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<TopologyDiscoveryWormhole>(soc_arch_descriptor, options);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<TopologyDiscoveryBlackhole>(soc_arch_descriptor, options);
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Unsupported architecture for topology discovery."));
    }
}

TopologyDiscoveryBase::TopologyDiscoveryBase(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor, const TopologyDiscoveryOptions& options) :
    options(options), soc_arch_descriptor_(std::move(soc_arch_descriptor)) {}

std::unique_ptr<ClusterDescriptor> TopologyDiscoveryBase::create_ethernet_map() {
    ZoneScopedC(tracy::Color::DarkGreen);
    log_info(LogUMD, "Starting topology discovery.");
    get_connected_devices();
    retrain_eth_cores();
    discover_remote_devices();
    tunnel_remote_devices_over_all_gateway_links();
    log_info(LogUMD, "Completed topology discovery.");
    return fill_cluster_descriptor_info();
}

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscoveryBase::run(
    const TopologyDiscoveryOptions& options) {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (options.simulation.has_value()) {
        // A simulator models PCIe and nothing else, so the devices discovered are PCIe-modelled whatever the caller
        // asked for. Accepting JTAG here would label the cluster descriptor with a transport none of its devices
        // speak; refuse instead of misreporting it.
        UMD_ASSERT(
            options.io_device_type == IODeviceType::PCIe,
            error::RuntimeError,
            fmt::format(
                "Simulation topology discovery models PCIe, but {} was requested.",
                DeviceTypeToString.at(options.io_device_type)));
        const std::filesystem::path simulator_path = options.simulation->simulator_path;
        // Checked before anything asks what simulator the path is: a server directory is not one.
        if (simulation_backend().is_server_directory(simulator_path)) {
            return discover_served_simulation_topology(options);
        }
        const SimulationBackendType backend = simulation_backend().get_backend_type(simulator_path);
        if (backend == SimulationBackendType::TTSIM) {
            const std::filesystem::path cluster_descriptor_path = get_simulator_cluster_descriptor_path(simulator_path);
            if (std::filesystem::exists(cluster_descriptor_path)) {
                return discover_declared_simulation_topology(options, cluster_descriptor_path);
            }
            // TTSim models no ARC or Ethernet for Quasar, so its image has nothing to walk (#3404).
            if (simulation_backend().get_simulator_arch(simulator_path) == tt::ARCH::QUASAR) {
                return discover_one_chip_simulation_topology(options, backend);
            }
        } else {
            const std::vector<std::string> layout_soc_descriptor_paths =
                simulation_backend().get_ip_layout_soc_descriptor_paths(simulator_path);
            if (layout_soc_descriptor_paths.empty()) {
                return discover_one_chip_simulation_topology(options, backend);
            }
            return discover_ip_layout_simulation_topology(options, layout_soc_descriptor_paths);
        }
    }

    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    std::unique_ptr<TopologyDiscoveryBase> td = TopologyDiscoveryBase::create_topology_discovery(options);
    if (td == nullptr) {
        // No PCI/JTAG devices: still stamp cluster_id (and throw on an illegal supplied id).
        // Callers and the Python bindings go through discover() even on an empty machine.
        ClusterDescription description;
        description.cluster_id = utils::resolve_cluster_id(options.cluster_id);
        return std::make_pair(create_cluster_descriptor(std::move(description)), std::move(devices));
    }
    std::unique_ptr<ClusterDescriptor> cluster_desc = td->create_ethernet_map();
    // Resort devices by ChipID instead of internal unique identifiers.
    for (auto& [unique_id, device] : td->devices) {
        ChipId chip_id = td->asic_id_to_chip_id[unique_id];
        devices[chip_id] = std::move(device);
    }
    if (!options.simulation.has_value()) {
        return std::make_pair(std::move(cluster_desc), std::move(devices));
    }
    // The whole image had to be opened to walk its links, so the chips this process discovers are picked only now.
    DiscoveredTopology selected =
        select_opened_simulated_chips({std::move(cluster_desc), std::move(devices)}, *options.simulation);
    // The walk is also what shows how many chips each device serves, so a channel count left to discovery is only
    // known now.
    if (!options.simulation->num_host_mem_channels.has_value()) {
        const int num_host_mem_channels = static_cast<int>(get_host_mem_channels_for_topology(*selected.first));
        for (auto& [chip_id, device] : selected.second) {
            simulation_backend().grow_host_mem_channels(*device, num_host_mem_channels);
        }
    }
    return selected;
}

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options) {
    return TopologyDiscoveryBase::run(options);
}

bool TopologyDiscoveryBase::init_device(TTDevice* tt_device, ChipId chip_id, const std::chrono::milliseconds timeout) {
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

void TopologyDiscoveryBase::add_local_device(std::unique_ptr<TTDevice> tt_device, int device_id) {
    ChipId chip_id = get_next_chip_id();

    // When coming out of reset, devices can take on the order of minutes to become ready.
    if (!init_device(tt_device.get(), chip_id, timeout::ARC_LONG_POST_RESET_TIMEOUT)) {
        uint64_t asic_id = generate_unhealthy_asic_id(chip_id);
        devices_to_discover.emplace(asic_id, std::move(tt_device));
        asic_id_to_chip_id.emplace(asic_id, chip_id);

        log_warning(
            LogUMD,
            "Discovered unhealthy {} device w/ MMIO, ID: {}, mocked ASIC ID: {}",
            DeviceTypeToString.at(options.io_device_type),
            device_id,
            asic_id);
        return;
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
        DeviceTypeToString.at(options.io_device_type),
        device_id,
        asic_id);
}

void TopologyDiscoveryBase::get_connected_devices() {
    ZoneScopedC(tracy::Color::DarkGreen);

    // Checked before the io_device_type switch below, and deliberately so: a simulator reports PCIe
    // as its transport, but create_tt_device(device_id, ...) would try to open /dev/tenstorrent for
    // it. A simulator image enumerates its own endpoints instead.
    if (options.simulation.has_value()) {
        // A count left to discovery starts at one channel per device and is grown in run() once the walk has shown
        // how many chips each device serves.
        for (auto& [chip_id, tt_device] : simulation_backend().create_local_simulation_tt_devices(
                 options.simulation->simulator_path, options.simulation->num_host_mem_channels.value_or(1))) {
            add_local_device(std::move(tt_device), chip_id);
        }
        log_debug(LogUMD, "Discovered {} simulated device(s).", devices_to_discover.size());
        return;
    }

    std::vector<int> local_device_ids;
    switch (options.io_device_type) {
        case IODeviceType::PCIe: {
            local_device_ids = PCIDevice::enumerate_devices();
            break;
        }
        default:
            UMD_THROW(error::RuntimeError, "Unsupported device type.");
    }

    for (auto& device_id : local_device_ids) {
        std::unique_ptr<TTDevice> tt_device =
            create_tt_device(device_id, options.io_device_type, options.use_safe_api, soc_arch_descriptor_);
        if (!options.low_power) {
            tt_device->set_power_state(TTDevice::PowerState::BUSY);
        }
        if (tt_device->get_arch() != get_topology_arch()) {
            log_warning(
                LogUMD,
                "Skipped device {} with different architecture: {}.",
                device_id,
                arch_to_str(tt_device->get_arch()));
            continue;
        }

        add_local_device(std::move(tt_device), device_id);
    }
    log_debug(LogUMD, "Discovered {} locally connected device(s).", devices_to_discover.size());
}

void TopologyDiscoveryBase::discover_remote_devices() {
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

void TopologyDiscoveryBase::tunnel_remote_devices_over_all_gateway_links() {
    std::unordered_map<TTDevice*, uint64_t> gateway_asic_ids;
    for (const auto& [asic_id, tt_device] : devices) {
        if (tt_device != nullptr && !tt_device->is_remote()) {
            gateway_asic_ids.emplace(tt_device.get(), asic_id);
        }
    }
    for (const auto& [asic_id, tt_device] : devices) {
        RemoteCommunication* remote_communication =
            tt_device != nullptr ? tt_device->get_remote_communication() : nullptr;
        if (remote_communication == nullptr) {
            continue;
        }
        TTDevice* gateway_device = remote_communication->get_local_device();
        remote_communication->set_remote_transfer_ethernet_cores(
            gateway_device->get_soc_descriptor().get_eth_xy_pairs_for_channels(
                active_eth_channels_per_device.at(gateway_asic_ids.at(gateway_device)), CoordSystem::TRANSLATED));
    }
}

std::unique_ptr<ClusterDescriptor> TopologyDiscoveryBase::fill_cluster_descriptor_info() {
    ClusterDescription description;

    // Workaround: a simulator reports PCIe with process-local ids, so skip host enumeration in simulation.
    const bool has_host_pci_devices = options.io_device_type == IODeviceType::PCIe && !options.simulation.has_value();
    const std::map<int, PciDeviceInfo> pci_devices_info =
        has_host_pci_devices ? PCIDevice::enumerate_devices_info() : std::map<int, PciDeviceInfo>{};

    // A remote device reports the communication id of the gateway it is reached through, so looking
    // it up here would attribute the gateway's BDF and bus to it. Only local devices have one.
    auto local_pci_info = [&pci_devices_info](TTDevice* tt_device) -> const PciDeviceInfo* {
        if (tt_device->is_remote()) {
            return nullptr;
        }
        auto it = pci_devices_info.find(tt_device->get_communication_device_id());
        return it == pci_devices_info.end() ? nullptr : &it->second;
    };

    for (const auto& [current_device_asic_id, tt_device] : devices) {
        ChipId chip_id = asic_id_to_chip_id.at(current_device_asic_id);

        description.health_errors.insert({chip_id, std::move(health_errors[current_device_asic_id])});

        // Cluster descriptor is not designed to contain partial information about devices,
        // so we cannot add information about unhealthy devices.
        if (is_marked_unhealthy(current_device_asic_id)) {
            description.unhealthy_chips.push_back(chip_id);
            continue;
        }

        ChipDescription& chip = description.chips[chip_id];
        chip.arch = tt_device->get_arch();
        chip.unique_id = current_device_asic_id;
        chip.board_type = tt_device->get_chip_info().board_type;
        chip.board_id = tt_device->get_chip_info().board_id;
        chip.noc_translation_enabled = tt_device->get_chip_info().noc_translation_enabled;
        chip.harvesting_masks = tt_device->get_chip_info().harvesting_masks;
        chip.asic_location = tt_device->get_chip_info().asic_location;

        if (!tt_device->is_remote()) {
            chip.mmio_device_id = tt_device->get_communication_device_id();
        }

        if (const PciDeviceInfo* pci_info = local_pci_info(tt_device.get())) {
            chip.pci_bus_id = pci_info->pci_bus;
        } else if (const std::optional<uint32_t> bdf = simulation_backend().get_pci_bdf(*tt_device)) {
            // Whatever bus the simulator placed the endpoint on, so tray and ASIC positions derive
            // from it as they do from a real bus id.
            chip.pci_bus_id = static_cast<uint16_t>((*bdf >> 8) & 0xFF);
        }

        // A simulated device reports PCIe but has no PCIDevice behind it, so its BDF is the one its
        // simulator image enumerated, spelled the way PCIDevice spells a real one.
        if (options.io_device_type == IODeviceType::PCIe && !tt_device->is_remote()) {
            if (const PciDeviceInfo* pci_info = local_pci_info(tt_device.get())) {
                chip.pci_bdf = pci_info->pci_bdf;
                chip.pcie_lane_count = tt_device->get_pcie_interface()->get_lane_count();
            } else if (const std::optional<uint32_t> bdf = simulation_backend().get_pci_bdf(*tt_device)) {
                // A simulated link has no trained lane count.
                chip.pci_bdf =
                    fmt::format("{:04x}:{:02x}:{:02x}.{:x}", 0, (*bdf >> 8) & 0xFF, (*bdf >> 3) & 0x1F, *bdf & 0x7);
            }
        }

        if (eth_coords.empty()) {
            chip.closest_mmio_chip = asic_id_to_chip_id.at(remote_asic_id_to_mmio_device_id.at(current_device_asic_id));
        }

        if (is_using_eth_coords() && !eth_coords.empty()) {
            chip.eth_coord = eth_coords.at(current_device_asic_id);
        }
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections) {
        description.eth_links.push_back(EthLink{
            asic_id_to_chip_id.at(ethernet_connection_logical.first),
            static_cast<EthernetChannel>(ethernet_connection_logical.second),
            asic_id_to_chip_id.at(ethernet_connection_remote.first),
            static_cast<EthernetChannel>(ethernet_connection_remote.second)});
    }

    for (auto [ethernet_connection_logical, ethernet_connection_remote] : ethernet_connections_to_remote_devices) {
        description.remote_eth_links.push_back(RemoteEthLink{
            asic_id_to_chip_id.at(ethernet_connection_logical.first),
            static_cast<EthernetChannel>(ethernet_connection_logical.second),
            ethernet_connection_remote.first,
            static_cast<EthernetChannel>(ethernet_connection_remote.second)});
    }

    for (const auto& [current_chip_asic_id, active_eth_channels] : active_eth_channels_per_device) {
        if (is_marked_unhealthy(current_chip_asic_id)) {
            continue;
        }
        const uint32_t num_eth_channels =
            devices.at(current_chip_asic_id)->get_soc_descriptor().get_cores(CoreType::ETH).size();
        ChipDescription& chip = description.chips.at(asic_id_to_chip_id.at(current_chip_asic_id));
        for (int i = 0; i < num_eth_channels; i++) {
            chip.idle_eth_channels.insert(i);
        }

        for (const auto& active_channel : active_eth_channels) {
            chip.active_eth_channels.insert(active_channel);
            chip.idle_eth_channels.erase(active_channel);
        }
    }
    // The caller-supplied cluster id, else the OS hostname. Stays unset if neither is usable.
    description.cluster_id = utils::resolve_cluster_id(options.cluster_id);
    description.io_device_type = options.io_device_type;
    description.eth_fw_version = expected_eth_fw_version;
    description.fw_bundle_version = first_fw_bundle_version;

    // A simulator's harvesting is whatever its image chose to model and need not match its board
    // type -- TTSim's single-chip Wormhole image reports an n150 with nothing harvested. The masks
    // still come from the device; only the board-level expectation is dropped.
    const bool check_harvesting_counts = !options.simulation.has_value();
    return create_cluster_descriptor(std::move(description), options.discover_remote_devices, check_harvesting_counts);
}

TTDevice* TopologyDiscoveryBase::get_tt_device(const uint64_t asic_id) {
    if (devices_to_discover.find(asic_id) != devices_to_discover.end()) {
        return devices_to_discover.at(asic_id).get();
    }
    return devices.at(asic_id).get();
}

uint64_t TopologyDiscoveryBase::get_asic_id(TTDevice* tt_device) {
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

void TopologyDiscoveryBase::patch_eth_connections() {}

void TopologyDiscoveryBase::verify_fw_bundle_version(TTDevice* tt_device, uint64_t asic_id) {
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
    FirmwareBundleVersion minimum_compatible_fw_bundle_version = get_minimum_compatible_firmware_version(arch);
    FirmwareBundleVersion latest_supported_fw_bundle_version = get_latest_supported_firmware_version(arch);
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

void TopologyDiscoveryBase::wait_eth_cores_training(TTDevice* tt_device, const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    log_debug(LogUMD, "Waiting on ethernet link training on device: {}", tt_device->get_communication_device_id());
    auto timeout_left = timeout_ms;
    const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
    const std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);
    for (const CoreCoord& eth_core : eth_cores) {
        tt_xy_pair actual_eth_core = soc_desc.translate_chip_coord_to_translated(eth_core, get_selected_noc_id());
        const auto start = std::chrono::steady_clock::now();
        tt_device->wait_eth_core_training(actual_eth_core, timeout_left);
        timeout_left -= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    }
    log_debug(
        LogUMD,
        "Completed ethernet link training on device: {} after {} ms.",
        tt_device->get_communication_device_id(),
        (timeout_ms - timeout_left).count());
}

bool TopologyDiscoveryBase::is_board_id_included(uint64_t board_id) const {
    return board_ids.find(board_id) != board_ids.end();
}

bool TopologyDiscoveryBase::verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
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

bool TopologyDiscoveryBase::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
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
            signature != erisc_firmware::METAL_FW_HEARTBEAT_SIGNATURE &&
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

bool TopologyDiscoveryBase::is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core) {
    return tt_device->read_eth_core_training_status(eth_core) == EthTrainingStatus::SUCCESS;
}

}  // namespace tt::umd
