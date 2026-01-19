// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/cluster_descriptor.hpp"

#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "api/umd/device/arch/blackhole_implementation.hpp"
#include "api/umd/device/arch/grendel_implementation.hpp"
#include "api/umd/device/arch/wormhole_implementation.hpp"
#include "api/umd/device/types/cluster_descriptor_types.hpp"
#include "assert.hpp"
#include "disjoint_set.hpp"

namespace tt::umd {

bool ClusterDescriptor::ethernet_core_has_active_ethernet_link(
    ChipId local_chip, EthernetChannel local_ethernet_channel) const {
    return (this->ethernet_connections.find(local_chip) != this->ethernet_connections.end() &&
            this->ethernet_connections.at(local_chip).find(local_ethernet_channel) !=
                this->ethernet_connections.at(local_chip).end()) ||
           (this->ethernet_connections_to_remote_devices.find(local_chip) !=
                this->ethernet_connections_to_remote_devices.end() &&
            this->ethernet_connections_to_remote_devices.at(local_chip).find(local_ethernet_channel) !=
                this->ethernet_connections_to_remote_devices.at(local_chip).end());
}

std::tuple<ChipId, EthernetChannel> ClusterDescriptor::get_chip_and_channel_of_remote_ethernet_core(
    ChipId local_chip, EthernetChannel local_ethernet_channel) const {
    std::vector<std::tuple<EthernetChannel, EthernetChannel>> directly_connected_channels = {};
    if (this->all_chips.find(local_chip) == this->all_chips.end() ||
        this->ethernet_connections.at(local_chip).find(local_ethernet_channel) ==
            this->ethernet_connections.at(local_chip).end()) {
        return {};
    }

    const auto &[connected_chip, connected_channel] =
        this->ethernet_connections.at(local_chip).at(local_ethernet_channel);
    if (this->all_chips.find(connected_chip) == this->all_chips.end()) {
        return {};
    } else {
        return {connected_chip, connected_channel};
    }
}

// NOTE: It might be worthwhile to precompute this for every pair of directly connected chips, depending on how
// extensively router needs to use it
std::vector<std::tuple<EthernetChannel, EthernetChannel>>
ClusterDescriptor::get_directly_connected_ethernet_channels_between_chips(
    const ChipId &first, const ChipId &second) const {
    std::vector<std::tuple<EthernetChannel, EthernetChannel>> directly_connected_channels = {};
    if (this->all_chips.find(first) == this->all_chips.end() || this->all_chips.find(second) == this->all_chips.end()) {
        return {};
    }

    for (const auto &[first_ethernet_channel, connected_chip_and_channel] : this->ethernet_connections.at(first)) {
        if (std::get<0>(connected_chip_and_channel) == second) {
            directly_connected_channels.push_back({first_ethernet_channel, std::get<1>(connected_chip_and_channel)});
        }
    }

    return directly_connected_channels;
}

bool ClusterDescriptor::is_chip_mmio_capable(const ChipId chip_id) const {
    return this->chips_with_mmio.find(chip_id) != this->chips_with_mmio.end();
}

bool ClusterDescriptor::is_chip_remote(const ChipId chip_id) const { return !is_chip_mmio_capable(chip_id); }

// Returns the closest mmio chip to the given chip.
ChipId ClusterDescriptor::get_closest_mmio_capable_chip(const ChipId chip) {
    log_debug(LogUMD, "get_closest_mmio_chip to chip{}", chip);

    if (this->is_chip_mmio_capable(chip)) {
        return chip;
    }

    if (closest_mmio_chip_cache.find(chip) != closest_mmio_chip_cache.end()) {
        return closest_mmio_chip_cache[chip];
    }

    const auto chips_on_the_same_board = get_board_chips(get_board_id_for_chip(chip));

    for (const ChipId &candidate_mmio_chip : chips_on_the_same_board) {
        if (is_chip_mmio_capable(candidate_mmio_chip)) {
            closest_mmio_chip_cache[chip] = candidate_mmio_chip;
            return candidate_mmio_chip;
        }
    }

    throw std::runtime_error(fmt::format("Closest mmio capable chip not found for chip {}.", chip));
}

std::unique_ptr<ClusterDescriptor> ClusterDescriptor::create_from_yaml(
    const std::string &cluster_descriptor_file_path) {
    std::ifstream fdesc(cluster_descriptor_file_path);
    if (fdesc.fail()) {
        throw std::runtime_error(fmt::format(
            "Error: cluster connectivity descriptor file {} does not exist!", cluster_descriptor_file_path));
    }
    std::stringstream buffer;
    buffer << fdesc.rdbuf();
    fdesc.close();
    return create_from_yaml_content(buffer.str());
}

std::unique_ptr<ClusterDescriptor> ClusterDescriptor::create_from_yaml_content(
    const std::string &cluster_descriptor_file_content) {
    std::unique_ptr<ClusterDescriptor> desc = std::make_unique<ClusterDescriptor>();

    YAML::Node yaml = YAML::Load(cluster_descriptor_file_content);
    desc->load_chips_from_connectivity_descriptor(yaml);
    desc->load_harvesting_information(yaml);
    desc->load_ethernet_connections_from_connectivity_descriptor(yaml);
    desc->merge_cluster_ids();

    desc->fill_chips_grouped_by_closest_mmio();

    desc->verify_cluster_descriptor_info();

    return desc;
}

template <typename T>
std::unordered_map<ChipId, T> filter_chip_collection(
    const std::unordered_map<ChipId, T> &collection, const std::unordered_set<ChipId> &chips) {
    std::unordered_map<ChipId, T> filtered_collection;
    for (const auto &[chip_id, val] : collection) {
        auto it = chips.find(chip_id);
        if (it != chips.end()) {
            filtered_collection.emplace(chip_id, val);
        }
    }
    return filtered_collection;
}

template <typename T>
std::map<ChipId, T> filter_chip_collection(
    const std::map<ChipId, T> &collection, const std::unordered_set<ChipId> &chips) {
    std::map<ChipId, T> filtered_collection;
    for (const auto &[chip_id, val] : collection) {
        auto it = chips.find(chip_id);
        if (it != chips.end()) {
            filtered_collection.emplace(chip_id, val);
        }
    }
    return filtered_collection;
}

template <typename T>
std::map<T, ChipId> filter_chip_collection(
    const std::map<T, ChipId> &collection, const std::unordered_set<ChipId> chips) {
    std::map<T, ChipId> filtered_collection;
    for (const auto &[val, chip_id] : collection) {
        auto it = chips.find(chip_id);
        if (it != chips.end()) {
            filtered_collection.emplace(val, chip_id);
        }
    }
    return filtered_collection;
}

std::unordered_set<ChipId> filter_chip_collection(
    const std::unordered_set<ChipId> &collection, const std::unordered_set<ChipId> &chips) {
    std::unordered_set<ChipId> filtered_collection;
    for (const auto &chip_id : collection) {
        auto it = chips.find(chip_id);
        if (it != chips.end()) {
            filtered_collection.emplace(chip_id);
        }
    }
    return filtered_collection;
}

std::unique_ptr<ClusterDescriptor> ClusterDescriptor::create_constrained_cluster_descriptor(
    const ClusterDescriptor *full_cluster_desc, const std::unordered_set<ChipId> &target_chip_ids) {
    std::unique_ptr<ClusterDescriptor> desc = std::make_unique<ClusterDescriptor>();

    desc->chip_locations = filter_chip_collection(full_cluster_desc->chip_locations, target_chip_ids);
    desc->chips_with_mmio = filter_chip_collection(full_cluster_desc->chips_with_mmio, target_chip_ids);
    desc->all_chips = filter_chip_collection(full_cluster_desc->all_chips, target_chip_ids);
    desc->noc_translation_enabled = filter_chip_collection(full_cluster_desc->noc_translation_enabled, target_chip_ids);
    // desc->closest_mmio_chip_cache is not copied intentionally, it could hold wrong information.
    desc->chip_board_type = filter_chip_collection(full_cluster_desc->chip_board_type, target_chip_ids);
    desc->chip_arch = filter_chip_collection(full_cluster_desc->chip_arch, target_chip_ids);
    desc->chip_unique_ids = filter_chip_collection(full_cluster_desc->chip_unique_ids, target_chip_ids);
    // Note that these preserve the full set of channels. So some channels will be reported as active
    // even though their corresponding entries won't be found in ethernet_connections. We want this behavior
    // so that the client doesn't try to do anything on these ETH cores which could break these links.
    desc->active_eth_channels = filter_chip_collection(full_cluster_desc->active_eth_channels, target_chip_ids);
    desc->idle_eth_channels = filter_chip_collection(full_cluster_desc->idle_eth_channels, target_chip_ids);

    desc->chip_to_bus_id = filter_chip_collection(full_cluster_desc->chip_to_bus_id, target_chip_ids);

    desc->harvesting_masks_map = filter_chip_collection(full_cluster_desc->harvesting_masks_map, target_chip_ids);

    desc->asic_locations = filter_chip_collection(full_cluster_desc->asic_locations, target_chip_ids);
    desc->io_device_type = full_cluster_desc->io_device_type;
    desc->eth_fw_version = full_cluster_desc->eth_fw_version;
    desc->fw_bundle_version = full_cluster_desc->fw_bundle_version;

    desc->chip_pci_bdfs = filter_chip_collection(full_cluster_desc->chip_pci_bdfs, target_chip_ids);

    // Write explicitly filters for more complex structures.
    for (const auto &[chip_id, eth_connections] : full_cluster_desc->ethernet_connections) {
        if (target_chip_ids.find(chip_id) == target_chip_ids.end()) {
            continue;
        }

        for (const auto &[eth_id, connection] : eth_connections) {
            const auto &[remote_chip_id, remote_eth_id] = connection;
            if (target_chip_ids.find(remote_chip_id) == target_chip_ids.end()) {
                continue;
            }
            desc->ethernet_connections[chip_id][eth_id] = {remote_chip_id, remote_eth_id};
        }
    }

    for (const auto &[rack_id, shelf_map] : full_cluster_desc->coords_to_chip_ids) {
        for (const auto &[shelf_id, y_map] : shelf_map) {
            for (const auto &[y_dim, x_map] : y_map) {
                for (const auto &[x_dim, chip_id] : x_map) {
                    if (target_chip_ids.find(chip_id) == target_chip_ids.end()) {
                        continue;
                    }
                    desc->coords_to_chip_ids[rack_id][shelf_id][y_dim][x_dim] = chip_id;
                }
            }
        }
    }

    for (const auto &[chip_id, chip_group] : full_cluster_desc->chips_grouped_by_closest_mmio) {
        if (target_chip_ids.find(chip_id) == target_chip_ids.end()) {
            continue;
        }

        desc->chips_grouped_by_closest_mmio[chip_id] = filter_chip_collection(chip_group, target_chip_ids);
    }

    return desc;
}

std::unique_ptr<ClusterDescriptor> ClusterDescriptor::create_mock_cluster(
    const std::unordered_set<ChipId> &logical_device_ids, tt::ARCH arch, bool noc_translation_enabled) {
    std::unique_ptr<ClusterDescriptor> desc = std::make_unique<ClusterDescriptor>();

    BoardType board_type;
    HarvestingMasks harvesting_masks{0, 0, 0, 0};
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            board_type = BoardType::N150;
            break;
        case tt::ARCH::QUASAR:  // TODO (#450): Add Quasar configuration
        case tt::ARCH::BLACKHOLE:
            board_type = BoardType::UNKNOWN;
            // Example value from silicon machine.
            harvesting_masks.eth_harvesting_mask = 0x120;
            break;
        default:
            board_type = BoardType::UNKNOWN;
            log_error(LogUMD, "Unsupported architecture for mock cluster");
            break;
    }

    for (auto &logical_id : logical_device_ids) {
        desc->all_chips.insert(logical_id);
        EthCoord chip_location{0, logical_id, 0, 0, 0};
        desc->chip_locations.insert({logical_id, chip_location});
        desc->coords_to_chip_ids[chip_location.rack][chip_location.shelf][chip_location.y][chip_location.x] =
            logical_id;
        log_debug(tt::LogUMD, "{} - adding logical: {}", __FUNCTION__, logical_id);
        desc->chip_board_type.insert({logical_id, board_type});
        desc->chips_with_mmio.insert({logical_id, logical_id});
        desc->chip_arch.insert({logical_id, arch});
        desc->chip_unique_ids.insert({logical_id, logical_id});
        desc->noc_translation_enabled.insert({logical_id, noc_translation_enabled});
        desc->harvesting_masks_map.insert({logical_id, harvesting_masks});
        desc->fill_mock_hardcoded_data(logical_id);
    }
    desc->fill_chips_grouped_by_closest_mmio();

    desc->verify_cluster_descriptor_info();

    return desc;
}

void ClusterDescriptor::fill_mock_hardcoded_data(ChipId logical_id) {
    // Populate a deterministic unique ASIC ID for mock/simulator clusters so downstream code relying on it
    // functions correctly.
    static constexpr uint64_t kSimUniqueIdBase = 0x5AA5000000000000ULL;
    this->chip_unique_ids.insert({logical_id, kSimUniqueIdBase + static_cast<uint64_t>(logical_id)});

    // Provide placeholder PCI bus IDs to align with host motherboard mappings when running tests that expect
    // realistic bus/tray associations. Use a known X12DPG-QT6 ordering and repeat if more than 4 devices.
    static const uint16_t kMockBusIds[4] = {0x00b1, 0x00ca, 0x0031, 0x004b};
    this->chip_to_bus_id.insert({logical_id, kMockBusIds[logical_id % 4]});

    // Provide a default ASIC location placeholder (0) for all chips; callers can override per-arch rules.
    this->asic_locations.insert({logical_id, static_cast<uint8_t>(0)});
}

void ClusterDescriptor::load_ethernet_connections_from_connectivity_descriptor(YAML::Node &yaml) {
    TT_ASSERT(yaml["ethernet_connections"].IsSequence(), "Invalid YAML");

    // Preload idle eth channels.
    for (const auto &chip : all_chips) {
        int num_harvested_channels =
            harvesting_masks_map.empty()
                ? 0
                : CoordinateManager::get_num_harvested(harvesting_masks_map.at(chip).eth_harvesting_mask);
        int num_channels =
            architecture_implementation::create(chip_arch.at(chip))->get_num_eth_channels() - num_harvested_channels;
        for (int i = 0; i < num_channels; i++) {
            idle_eth_channels[chip].insert(i);
        }
    }

    for (YAML::Node &connected_endpoints : yaml["ethernet_connections"].as<std::vector<YAML::Node>>()) {
        TT_ASSERT(connected_endpoints.IsSequence(), "Invalid YAML");

        std::vector<YAML::Node> endpoints = connected_endpoints.as<std::vector<YAML::Node>>();
        TT_ASSERT(
            endpoints.size() <= 3,
            "Ethernet connections in YAML should always contatin information on connected endpoints and optionally "
            "information on whether "
            "routing is enabled.");

        int chip_0 = endpoints.at(0)["chip"].as<int>();
        int channel_0 = endpoints.at(0)["chan"].as<int>();
        int chip_1 = endpoints.at(1)["chip"].as<int>();
        int channel_1 = endpoints.at(1)["chan"].as<int>();
        auto &eth_conn_chip_0 = ethernet_connections.at(chip_0);
        if (eth_conn_chip_0.find(channel_0) != eth_conn_chip_0.end()) {
            TT_ASSERT(
                (std::get<0>(eth_conn_chip_0.at(channel_0)) == chip_1) &&
                    (std::get<1>(eth_conn_chip_0.at(channel_0)) == channel_1),
                "Duplicate eth connection found in cluster desc yaml");
        } else {
            eth_conn_chip_0.insert({channel_0, {chip_1, channel_1}});
        }
        auto &eth_conn_chip_1 = ethernet_connections.at(chip_1);
        if (eth_conn_chip_1.find(channel_1) != eth_conn_chip_1.end()) {
            TT_ASSERT(
                (std::get<0>(eth_conn_chip_1.at(channel_1)) == chip_0) &&
                    (std::get<1>(eth_conn_chip_1.at(channel_1)) == channel_0),
                "Duplicate eth connection found in cluster desc yaml");
        } else {
            eth_conn_chip_1.insert({channel_1, {chip_0, channel_0}});
        }
        active_eth_channels[chip_0].insert(channel_0);
        idle_eth_channels[chip_0].erase(channel_0);
        active_eth_channels[chip_1].insert(channel_1);
        idle_eth_channels[chip_1].erase(channel_1);
    }

    // std::unordered_map<EthernetChannel, std::tuple<ChipId, EthernetChannel>>> ethernet_connections;

    log_debug(LogUMD, "Ethernet Connectivity Descriptor:");
    for (const auto &[chip, chan_to_chip_chan_map] : ethernet_connections) {
        for (const auto &[chan, chip_and_chan] : chan_to_chip_chan_map) {
            log_debug(
                LogUMD,
                "\tchip: {}, chan: {}  <-->  chip: {}, chan: {}",
                chip,
                chan,
                std::get<0>(chip_and_chan),
                std::get<1>(chip_and_chan));
        }
    }

    log_debug(LogUMD, "Chip Coordinates:");
    for (const auto &[rack_id, rack_chip_map] : coords_to_chip_ids) {
        for (const auto &[shelf_id, shelf_chip_map] : rack_chip_map) {
            log_debug(LogUMD, "\tRack:{} Shelf:{}", rack_id, shelf_id);
            for (const auto &[row, row_chip_map] : shelf_chip_map) {
                std::stringstream row_chips;
                for (const auto &[col, chip_id] : row_chip_map) {
                    row_chips << chip_id << "\t";
                }
                log_debug(LogUMD, "\t\t{}", row_chips.str());
            }
        }
    }

    if (yaml["ethernet_connections_to_remote_devices"].IsDefined()) {
        for (YAML::Node &connected_endpoints :
             yaml["ethernet_connections_to_remote_devices"].as<std::vector<YAML::Node>>()) {
            TT_ASSERT(connected_endpoints.IsSequence(), "Invalid YAML");

            std::vector<YAML::Node> endpoints = connected_endpoints.as<std::vector<YAML::Node>>();
            TT_ASSERT(
                endpoints.size() == 2,
                "Remote ethernet connections in YAML should always contatin information on connected endpoints and "
                "channels");

            ChipId chip_0 = endpoints.at(0)["chip"].as<ChipId>();
            int channel_0 = endpoints.at(0)["chan"].as<int>();
            uint64_t chip_1 = endpoints.at(1)["remote_chip_id"].as<uint64_t>();
            int channel_1 = endpoints.at(1)["chan"].as<int>();
            ethernet_connections_to_remote_devices[chip_0][channel_0] = {chip_1, channel_1};

            // Mark the local channel as active and remove from idle, to accurately represent used Ethernet channels in
            // mock clusters (matching real hardware discovery)
            active_eth_channels[chip_0].insert(channel_0);
            idle_eth_channels[chip_0].erase(channel_0);
        }
    }
}

void ClusterDescriptor::merge_cluster_ids() {
    DisjointSet<ChipId> chip_sets;
    for (const auto &[chip, _] : chip_locations) {
        chip_sets.add_item(chip);
        log_trace(LogUMD, "Adding chip {} to disjoint set", chip);
    }

    for (const auto &[chip, chan_to_chip_chan_map] : ethernet_connections) {
        for (const auto &[chan, dest_chip_chan_tuple] : chan_to_chip_chan_map) {
            chip_sets.merge(chip, std::get<0>(dest_chip_chan_tuple));
            log_trace(LogUMD, "Merging chip {} and chip {}", chip, std::get<0>(dest_chip_chan_tuple));
        }
    }

    for (const auto &[chip, chip_eth_coords] : chip_locations) {
        chip_locations[chip].cluster_id = chip_sets.get_set(chip);
        log_trace(LogUMD, "Chip {} belongs to cluster {}", chip, chip_sets.get_set(chip));
    }
}

void ClusterDescriptor::load_chips_from_connectivity_descriptor(YAML::Node &yaml) {
    for (YAML::const_iterator node = yaml["arch"].begin(); node != yaml["arch"].end(); ++node) {
        ChipId chip_id = node->first.as<int>();
        std::string arch_str = node->second.as<std::string>();
        all_chips.insert(chip_id);
        chip_arch.insert({chip_id, tt::arch_from_str(arch_str)});
        ethernet_connections.insert({chip_id, {}});
    }

    for (YAML::const_iterator node = yaml["chips"].begin(); node != yaml["chips"].end(); ++node) {
        ChipId chip_id = node->first.as<int>();
        std::vector<int> chip_rack_coords = node->second.as<std::vector<int>>();
        TT_ASSERT(chip_rack_coords.size() == 4, "Galaxy (x, y, rack, shelf) coords must be size 4");
        EthCoord chip_location{
            chip_id, chip_rack_coords.at(0), chip_rack_coords.at(1), chip_rack_coords.at(2), chip_rack_coords.at(3)};

        chip_locations.insert({chip_id, chip_location});
        coords_to_chip_ids[chip_location.rack][chip_location.shelf][chip_location.y][chip_location.x] = chip_id;
    }

    for (const auto &chip : yaml["chips_with_mmio"]) {
        if (chip.IsMap()) {
            const auto &chip_map = chip.as<std::map<ChipId, ChipId>>();
            const auto &chips = chip_map.begin();
            chips_with_mmio.insert({chips->first, chips->second});
        } else {
            const auto &chip_val = chip.as<int>();
            chips_with_mmio.insert({chip_val, chip_val});
        }
    }
    log_debug(LogUMD, "Device IDs and Locations:");
    for (const auto &[chip_id, chip_location] : chip_locations) {
        log_debug(LogUMD, "\tchip: {}, coord: {}", chip_id, chip_location);
    }

    if (yaml["chip_to_boardtype"]) {
        for (const auto &yaml_chip_board_type : yaml["chip_to_boardtype"].as<std::map<int, std::string>>()) {
            auto &chip = yaml_chip_board_type.first;
            const std::string &board_type_str = yaml_chip_board_type.second;
            BoardType board_type = board_type_from_string(board_type_str);
            if (board_type == BoardType::UNKNOWN) {
                log_warning(
                    LogUMD,
                    "Unknown board type for chip {}. This might happen because chip is running old firmware. "
                    "Defaulting to UNKNOWN",
                    chip);
            }
            chip_board_type.insert({chip, board_type});
        }
    } else if (yaml["boardtype"]) {
        // Legacy format support: parse old "boardtype" field for backward compatibility.
        for (const auto &yaml_chip_board_type : yaml["boardtype"].as<std::map<int, std::string>>()) {
            auto &chip = yaml_chip_board_type.first;
            const std::string &board_type_str = yaml_chip_board_type.second;
            BoardType board_type = board_type_from_string(board_type_str);
            if (board_type == BoardType::UNKNOWN) {
                log_warning(
                    LogUMD,
                    "Unknown board type for chip {} from legacy boardtype field. "
                    "Defaulting to UNKNOWN",
                    chip);
            }
            chip_board_type.insert({chip, board_type});
        }
    } else {
        for (const auto &chip : all_chips) {
            chip_board_type.insert({chip, BoardType::UNKNOWN});
        }
    }

    if (yaml["boards"]) {
        YAML::Node boardsNode = yaml["boards"];
        if (!boardsNode || !boardsNode.IsSequence()) {
            throw std::runtime_error("Invalid or missing 'boards' node.");
        }

        for (const auto &boardEntry : boardsNode) {
            if (!boardEntry.IsSequence() || boardEntry.size() != 3) {
                throw std::runtime_error("Each board entry should be a sequence of 3 maps.");
            }

            uint64_t board_id = boardEntry[0]["board_id"].as<std::uint64_t>();

            for (const auto &chip : boardEntry[2]["chips"]) {
                add_chip_to_board(chip.as<ChipId>(), board_id);
            }
        }
    }

    if (yaml["chip_unique_ids"]) {
        for (const auto &chip_unique_id : yaml["chip_unique_ids"].as<std::map<int, uint64_t>>()) {
            auto &chip = chip_unique_id.first;
            auto &unique_id = chip_unique_id.second;
            chip_unique_ids.insert({chip, unique_id});
        }
    } else {
        // Legacy format or mock descriptors may not have chip_unique_ids
        // Generate synthetic IDs for backward compatibility.
        for (const auto &chip : all_chips) {
            // Use chip ID shifted left to create unique synthetic IDs.
            chip_unique_ids.insert({chip, static_cast<uint64_t>(chip) << 32});
        }
    }

    if (yaml["chip_to_bus_id"]) {
        for (const auto &chip_bus_id : yaml["chip_to_bus_id"].as<std::map<int, std::string>>()) {
            auto &chip = chip_bus_id.first;
            std::string bus_str = chip_bus_id.second;

            // Enforce '0x' prefix.
            if (bus_str.substr(0, 2) != "0x") {
                std::string msg =
                    "Bus string without 0x prefix for chip " + std::to_string(chip) + ": \"" + bus_str + "\"";
                throw std::runtime_error(msg);
            }
            bus_str = bus_str.substr(2);

            uint16_t bus_id = static_cast<uint16_t>(std::stoul(bus_str, nullptr, 16));
            chip_to_bus_id.insert({chip, bus_id});
        }
    }

    if (yaml["asic_locations"]) {
        for (const auto &chip_asic_locations : yaml["asic_locations"].as<std::map<int, uint64_t>>()) {
            auto &chip = chip_asic_locations.first;
            auto &asic_location = chip_asic_locations.second;
            asic_locations.insert({chip, asic_location});
        }
    }

    if (yaml["chip_pci_bdfs"]) {
        for (const auto &chip_pci_bdf : yaml["chip_pci_bdfs"].as<std::map<int, std::string>>()) {
            auto &chip = chip_pci_bdf.first;
            const std::string &bdf_str = chip_pci_bdf.second;

            // make sure chip is mmio mapped
            if (chips_with_mmio.find(chip) == chips_with_mmio.end()) {
                throw std::runtime_error(fmt::format("Chip {} has PCI BDF specified but is not mmio mapped.", chip));
            }

            chip_pci_bdfs.insert({chip, bdf_str});
        }
    }
}

void ClusterDescriptor::load_harvesting_information(YAML::Node &yaml) {
    if (yaml["harvesting"]) {
        for (const auto &chip_node : yaml["harvesting"].as<std::map<int, YAML::Node>>()) {
            ChipId chip = chip_node.first;
            auto harvesting_info = chip_node.second;
            noc_translation_enabled.insert({chip, harvesting_info["noc_translation"].as<bool>()});

            HarvestingMasks harvesting{0, 0, 0, 0};

            harvesting.tensix_harvesting_mask = harvesting_info["harvest_mask"].as<std::uint32_t>();

            if (harvesting_info["dram_harvesting_mask"].IsDefined()) {
                harvesting.dram_harvesting_mask = harvesting_info["dram_harvesting_mask"].as<std::uint32_t>();
            }

            if (harvesting_info["eth_harvesting_mask"].IsDefined()) {
                harvesting.eth_harvesting_mask = harvesting_info["eth_harvesting_mask"].as<std::uint32_t>();
            }

            if (harvesting_info["pcie_harvesting_mask"].IsDefined()) {
                harvesting.pcie_harvesting_mask = harvesting_info["pcie_harvesting_mask"].as<std::uint32_t>();
            }

            if (harvesting_info["l2cpu_harvesting_mask"].IsDefined()) {
                harvesting.l2cpu_harvesting_mask = harvesting_info["l2cpu_harvesting_mask"].as<std::uint32_t>();
            }

            harvesting_masks_map.insert({chip, harvesting});
        }
    }
}

void ClusterDescriptor::fill_chips_grouped_by_closest_mmio() {
    for (const auto &chip : this->all_chips) {
        if (this->is_chip_mmio_capable(chip)) {
            this->chips_grouped_by_closest_mmio[chip].insert(chip);
            continue;
        }
        // TODO: This is to handle the case when we are not using ETH coordinates and have remote chip.
        // Obviously, we have to figure out how to handle these cases in general in the future.
        if (this->chip_locations.empty()) {
            continue;
        }
        ChipId closest_mmio_chip = get_closest_mmio_capable_chip(chip);
        this->chips_grouped_by_closest_mmio[closest_mmio_chip].insert(chip);
    }
}

const std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<ChipId, EthernetChannel>>> &
ClusterDescriptor::get_ethernet_connections() const {
    return ethernet_connections;
}

const std::unordered_map<ChipId, std::unordered_map<EthernetChannel, std::tuple<uint64_t, EthernetChannel>>> &
ClusterDescriptor::get_ethernet_connections_to_remote_devices() const {
    return this->ethernet_connections_to_remote_devices;
}

EthCoord ClusterDescriptor::get_chip_location(const ChipId chip) const {
    if (chip_locations.find(chip) == chip_locations.end()) {
        return {0, 0, 0, 0};
    }
    return chip_locations.at(chip);
}

const std::unordered_map<ChipId, EthCoord> &ClusterDescriptor::get_chip_locations() const { return chip_locations; }

// Note: this API works only for Wormhole 6U galaxy at the moment.
// TODO: implement this for Blackhole and old Wormhole configurations.
const std::unordered_map<ChipId, uint64_t> &ClusterDescriptor::get_chip_unique_ids() const { return chip_unique_ids; }

ChipId ClusterDescriptor::get_shelf_local_physical_chip_coords(ChipId virtual_coord) {
    TT_ASSERT(
        !this->chip_locations.empty(),
        "Getting physical chip coordinates is only valid for systems where chips have coordinates");
    // NoC 0 coordinates of chip inside a single rack. Calculated based on Galaxy topology.
    // See:
    // https://yyz-gitlab.local.tenstorrent.com/tenstorrent/budabackend/-/wikis/uploads/23e7a5168f38dfb706f9887fde78cb03/image.png
    int x = get_chip_locations().at(virtual_coord).x;
    int y = get_chip_locations().at(virtual_coord).y;
    return 8 * x + y;
}

// Return map, but filter by enabled active chips.
const std::unordered_map<ChipId, ChipId> &ClusterDescriptor::get_chips_with_mmio() const { return chips_with_mmio; }

const std::unordered_set<ChipId> &ClusterDescriptor::get_all_chips() const { return this->all_chips; }

std::vector<ChipId> ClusterDescriptor::get_chips_local_first(const std::unordered_set<ChipId> &chips) const {
    std::vector<ChipId> chips_local_first;
    for (const auto &chip : chips) {
        TT_ASSERT(
            this->all_chips.find(chip) != this->all_chips.end(), "Chip {} not found in cluster descriptor.", chip);
    }
    for (const auto &chip : chips) {
        if (is_chip_mmio_capable(chip)) {
            chips_local_first.push_back(chip);
        }
    }
    for (const auto &chip : chips) {
        if (is_chip_remote(chip)) {
            chips_local_first.push_back(chip);
        }
    }
    return chips_local_first;
}

const std::unordered_map<ChipId, bool> &ClusterDescriptor::get_noc_translation_table_en() const {
    return noc_translation_enabled;
}

std::size_t ClusterDescriptor::get_number_of_chips() const { return this->all_chips.size(); }

BoardType ClusterDescriptor::get_board_type(ChipId chip_id) const {
    TT_ASSERT(
        chip_board_type.find(chip_id) != chip_board_type.end(),
        "Chip {} does not have a board type in the cluster descriptor",
        chip_id);
    return chip_board_type.at(chip_id);
}

tt::ARCH ClusterDescriptor::get_arch() const {
    const std::unordered_set<ChipId> &chips = get_all_chips();
    if (chips.empty()) {
        TT_THROW("Unable to determine architecture because no chips were detected.");
    }

    // We already validated that all chips have the same arch.
    tt::ARCH arch = get_arch(*chips.begin());
    if (arch == tt::ARCH::Invalid) {
        TT_THROW("Chip {} has invalid architecture.", *chips.begin());
    }
    return arch;
}

tt::ARCH ClusterDescriptor::get_arch(ChipId chip_id) const {
    TT_ASSERT(
        chip_arch.find(chip_id) != chip_arch.end(),
        "Chip {} does not have an architecture in the cluster descriptor",
        chip_id);
    return chip_arch.at(chip_id);
}

const std::unordered_map<ChipId, std::unordered_set<ChipId>> &ClusterDescriptor::get_chips_grouped_by_closest_mmio()
    const {
    return chips_grouped_by_closest_mmio;
}

std::string ClusterDescriptor::serialize() const {
    YAML::Emitter out;

    out << YAML::BeginMap;

    out << YAML::Key << "arch" << YAML::Value << YAML::BeginMap;
    std::map<ChipId, tt::ARCH> chip_arch_map = std::map<ChipId, tt::ARCH>(chip_arch.begin(), chip_arch.end());
    for (const auto &[chip_id, arch] : chip_arch_map) {
        out << YAML::Key << chip_id << YAML::Value << tt::arch_to_str(arch);
    }
    out << YAML::EndMap;

    out << YAML::Key << "chips" << YAML::Value << YAML::BeginMap;
    std::map<ChipId, EthCoord> chip_locations_map =
        std::map<ChipId, EthCoord>(chip_locations.begin(), chip_locations.end());
    for (const auto &[chip_id, chip_location] : chip_locations_map) {
        out << YAML::Key << chip_id << YAML::Value << YAML::BeginSeq << chip_location.x << chip_location.y
            << chip_location.rack << chip_location.shelf << YAML::EndSeq;
    }
    out << YAML::EndMap;

    out << YAML::Key << "chip_unique_ids" << YAML::Value << YAML::BeginMap;
    for (const auto &[chip_id, unique_id] : chip_unique_ids) {
        out << YAML::Key << chip_id << YAML::Value << unique_id;
    }
    out << YAML::EndMap;

    out << YAML::Key << "ethernet_connections" << YAML::Value << YAML::BeginSeq;
    std::set<std::pair<std::pair<ChipId, int>, std::pair<ChipId, int>>> all_connections;
    for (const auto &[src_chip, channels] : ethernet_connections) {
        for (const auto &[src_chan, dest] : channels) {
            auto [dest_chip, dest_chan] = dest;
            all_connections.insert(
                std::make_pair(std::make_pair(src_chip, src_chan), std::make_pair(dest_chip, dest_chan)));
        }
    }
    std::set<std::pair<ChipId, int>> serialized_connections;
    for (const auto &[src, dest] : all_connections) {
        auto [src_chip, src_chan] = src;
        if (serialized_connections.find({src_chip, src_chan}) != serialized_connections.end()) {
            continue;
        }
        auto [dest_chip, dest_chan] = dest;
        serialized_connections.insert({dest_chip, dest_chan});
        out << YAML::BeginSeq;
        out << YAML::BeginMap << YAML::Key << "chip" << YAML::Value << src_chip << YAML::Key << "chan" << YAML::Value
            << src_chan << YAML::EndMap;
        out << YAML::BeginMap << YAML::Key << "chip" << YAML::Value << dest_chip << YAML::Key << "chan" << YAML::Value
            << dest_chan << YAML::EndMap;
        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "ethernet_connections_to_remote_devices" << YAML::Value << YAML::BeginSeq;
    for (const auto &[src_chip, channels] : ethernet_connections_to_remote_devices) {
        for (const auto &[src_chan, dest] : channels) {
            auto [dest_chip, dest_chan] = dest;
            out << YAML::BeginSeq;
            out << YAML::BeginMap << YAML::Key << "chip" << YAML::Value << src_chip << YAML::Key << "chan"
                << YAML::Value << src_chan << YAML::EndMap;
            out << YAML::BeginMap << YAML::Key << "remote_chip_id" << YAML::Value << dest_chip << YAML::Key << "chan"
                << YAML::Value << dest_chan << YAML::EndMap;
            out << YAML::EndSeq;
        }
    }
    out << YAML::EndSeq;

    out << YAML::Key << "chips_with_mmio" << YAML::Value << YAML::BeginSeq;
    std::map<ChipId, ChipId> chips_with_mmio_map =
        std::map<ChipId, ChipId>(chips_with_mmio.begin(), chips_with_mmio.end());
    for (const auto &chip_with_mmio : chips_with_mmio_map) {
        out << YAML::BeginMap << YAML::Key << chip_with_mmio.first << YAML::Value << chip_with_mmio.second
            << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "io_device_type" << YAML::Value << DeviceTypeToString.at(io_device_type);

    out << YAML::Key << "harvesting" << YAML::Value << YAML::BeginMap;
    std::set<ChipId> all_chips_map = std::set<ChipId>(all_chips.begin(), all_chips.end());
    for (const int &chip : all_chips_map) {
        out << YAML::Key << chip << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "noc_translation" << YAML::Value << noc_translation_enabled.at(chip);
        HarvestingMasks harvesting = get_harvesting_masks(chip);
        out << YAML::Key << "harvest_mask" << YAML::Value << harvesting.tensix_harvesting_mask;
        out << YAML::Key << "dram_harvesting_mask" << YAML::Value << harvesting.dram_harvesting_mask;
        out << YAML::Key << "eth_harvesting_mask" << YAML::Value << harvesting.eth_harvesting_mask;
        out << YAML::Key << "pcie_harvesting_mask" << YAML::Value << harvesting.pcie_harvesting_mask;
        out << YAML::Key << "l2cpu_harvesting_mask" << YAML::Value << harvesting.l2cpu_harvesting_mask;
        out << YAML::EndMap;
    }
    out << YAML::EndMap;

    out << YAML::Key << "chip_to_boardtype" << YAML::Value << YAML::BeginMap;
    for (const int &chip : all_chips_map) {
        out << YAML::Key << chip << YAML::Value << board_type_to_string(chip_board_type.at(chip));
    }
    out << YAML::EndMap;

    out << YAML::Key << "chip_to_bus_id" << YAML::Value << YAML::BeginMap;
    std::map<ChipId, uint16_t> sorted_chip_to_bus_id(chip_to_bus_id.begin(), chip_to_bus_id.end());
    for (const auto &[chip, bus_id] : sorted_chip_to_bus_id) {
        std::string hex_bus_id = fmt::format("0x{:04x}", bus_id);
        out << YAML::Key << chip << YAML::Value << hex_bus_id;
    }
    out << YAML::EndMap;

    out << YAML::Key << "boards" << YAML::Value << YAML::BeginSeq;
    for (const auto &[board_id, chips] : board_to_chips) {
        out << YAML::BeginSeq;
        out << YAML::BeginMap << YAML::Key << "board_id" << YAML::Value << board_id << YAML::EndMap;
        out << YAML::BeginMap << YAML::Key << "board_type" << YAML::Value
            << board_type_to_string(get_board_type_from_board_id(board_id)) << YAML::EndMap;

        out << YAML::BeginMap << YAML::Key << "chips" << YAML::Value;
        out << YAML::BeginSeq;
        for (const auto &chip_id : chips) {
            out << chip_id;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;

        out << YAML::EndSeq;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "asic_locations" << YAML::Value << YAML::BeginMap;
    std::map<ChipId, uint8_t> asic_locations_map =
        std::map<ChipId, uint8_t>(asic_locations.begin(), asic_locations.end());
    for (const auto &[chip_id, asic_location] : asic_locations_map) {
        out << YAML::Key << chip_id << YAML::Value << static_cast<int>(asic_location);
    }
    out << YAML::EndMap;

    out << YAML::Key << "chip_pci_bdfs" << YAML::Value << YAML::BeginMap;
    std::map<ChipId, std::string> pci_bdfs_map =
        std::map<ChipId, std::string>(chip_pci_bdfs.begin(), chip_pci_bdfs.end());
    for (const auto &[chip_id, bdf] : pci_bdfs_map) {
        out << YAML::Key << chip_id << YAML::Value << bdf;
    }
    out << YAML::EndMap;

    out << YAML::EndMap;

    return out.c_str();
}

std::filesystem::path ClusterDescriptor::serialize_to_file(const std::filesystem::path &dest_file) const {
    std::filesystem::path file_path = dest_file;
    if (file_path.empty()) {
        file_path = get_default_cluster_descriptor_file_path();
    }
    std::ofstream file(file_path);
    file << serialize();
    file.close();
    return file_path;
}

std::filesystem::path ClusterDescriptor::get_default_cluster_descriptor_file_path() const {
    std::filesystem::path temp_path = std::filesystem::temp_directory_path();
    std::string cluster_path_dir_template = temp_path / "umd_XXXXXX";
    std::filesystem::path cluster_path_dir = mkdtemp(cluster_path_dir_template.data());
    std::filesystem::path cluster_path = cluster_path_dir / "cluster_descriptor.yaml";

    return cluster_path;
}

std::set<uint32_t> ClusterDescriptor::get_active_eth_channels(ChipId chip_id) {
    auto it = active_eth_channels.find(chip_id);
    if (it == active_eth_channels.end()) {
        return {};
    }

    return it->second;
}

std::set<uint32_t> ClusterDescriptor::get_idle_eth_channels(ChipId chip_id) {
    auto it = idle_eth_channels.find(chip_id);
    if (it == idle_eth_channels.end()) {
        return {};
    }

    return it->second;
}

HarvestingMasks ClusterDescriptor::get_harvesting_masks(ChipId chip_id) const {
    auto it = harvesting_masks_map.find(chip_id);
    if (it == harvesting_masks_map.end()) {
        return HarvestingMasks{0, 0, 0, 0, 0};
    }
    return it->second;
}

void ClusterDescriptor::add_chip_to_board(ChipId chip_id, uint64_t board_id) {
    if (chip_to_board_id.find(chip_id) != chip_to_board_id.end() && chip_to_board_id[chip_id] != board_id) {
        throw std::runtime_error(
            fmt::format("Chip {} is already mapped to board {:#x}", chip_id, chip_to_board_id[chip_id]));
    }
    chip_to_board_id[chip_id] = board_id;
    board_to_chips[board_id].insert(chip_id);
}

uint64_t ClusterDescriptor::get_board_id_for_chip(const ChipId chip) const {
    auto it = chip_to_board_id.find(chip);
    if (it != chip_to_board_id.end()) {
        return it->second;
    }
    throw std::runtime_error(fmt::format("Chip to board mapping for chip {} not found.", chip));
}

std::unordered_set<ChipId> ClusterDescriptor::get_board_chips(const uint64_t board_id) const {
    auto it = board_to_chips.find(board_id);
    if (it != board_to_chips.end()) {
        return it->second;
    }
    throw std::runtime_error(fmt::format("Board to chips mapping for board {:#x} not found.", board_id));
}

bool ClusterDescriptor::verify_board_info_for_chips() {
    bool board_info_good = true;
    for (const ChipId chip : all_chips) {
        if (!chip_to_board_id.empty() && chip_to_board_id.find(chip) == chip_to_board_id.end()) {
            log_warning(LogUMD, "Chip {} does not have a board ID assigned.", chip);
            board_info_good = false;
        }
    }

    for (const auto &[board_id, chips] : board_to_chips) {
        const BoardType board_type = get_board_type_from_board_id(board_id);
        const uint32_t number_chips_from_board = get_number_of_chips_from_board_type(board_type);
        if (chips.size() != number_chips_from_board) {
            log_warning(
                LogUMD,
                "Board {:#x} has {} chips, but expected {} chips for board type {}.",
                board_id,
                chips.size(),
                number_chips_from_board,
                board_type_to_string(board_type));
            board_info_good = false;
        }
    }

    return board_info_good;
}

bool ClusterDescriptor::verify_same_architecture() {
    const std::unordered_set<ChipId> &chips = get_all_chips();
    if (!chips.empty()) {
        tt::ARCH arch = get_arch(*chips.begin());
        if (arch == tt::ARCH::Invalid) {
            TT_THROW("Chip {} has invalid architecture.", *chips.begin());
        }
        bool all_same_arch =
            std::all_of(chips.begin(), chips.end(), [&](ChipId chip_id) { return this->get_arch(chip_id) == arch; });
        if (!all_same_arch) {
            TT_THROW("Chips with differing architectures detected. This is unsupported.");
        }
    }

    return true;
}

bool ClusterDescriptor::verify_harvesting_information() {
    bool harvesting_info_good = true;

    for (const ChipId chip : all_chips) {
        HarvestingMasks harvesting_masks = get_harvesting_masks(chip);

        const BoardType board_type = get_board_type(chip);

        uint32_t expected_tensix_harvested_units =
            expected_tensix_harvested_units_map.find(board_type) != expected_tensix_harvested_units_map.end()
                ? expected_tensix_harvested_units_map.at(board_type)
                : 0;

        uint32_t actual_tensix_harvested_units =
            CoordinateManager::get_num_harvested(harvesting_masks.tensix_harvesting_mask);

        if (expected_tensix_harvested_units != actual_tensix_harvested_units) {
            log_warning(
                LogUMD,
                "Chip {} has inconsistent Tensix harvesting information between harvest mask and number of harvested. "
                "Board {} expects {} units, but harvest mask indicates {} units.",
                chip,
                board_type_to_string(board_type),
                expected_tensix_harvested_units,
                actual_tensix_harvested_units);
            harvesting_info_good = false;
        }

        uint32_t expected_dram_harvested_units =
            expected_dram_harvested_units_map.find(board_type) != expected_dram_harvested_units_map.end()
                ? expected_dram_harvested_units_map.at(board_type)
                : 0;
        uint32_t actual_dram_harvested_units =
            CoordinateManager::get_num_harvested(harvesting_masks.dram_harvesting_mask);

        if (expected_dram_harvested_units != actual_dram_harvested_units) {
            log_warning(
                LogUMD,
                "Chip {} has inconsistent DRAM harvesting information between harvest mask and number of harvested. "
                "Board {} expects {} units, but harvesting mask indicates {} units.",
                chip,
                board_type_to_string(board_type),
                expected_dram_harvested_units,
                actual_dram_harvested_units);
            harvesting_info_good = false;
        }

        uint32_t expected_eth_harvested_units =
            expected_eth_harvested_units_map.find(board_type) != expected_eth_harvested_units_map.end()
                ? expected_eth_harvested_units_map.at(board_type)
                : 0;
        uint32_t actual_eth_harvested_units =
            CoordinateManager::get_num_harvested(harvesting_masks.eth_harvesting_mask);

        if (expected_eth_harvested_units != actual_eth_harvested_units) {
            log_warning(
                LogUMD,
                "Chip {} has inconsistent ETH harvesting information between harvest mask and number of harvested. "
                "Board {} expects {} units, but harvesting mask indicates {} units.",
                chip,
                board_type_to_string(board_type),
                expected_eth_harvested_units,
                actual_eth_harvested_units);
            harvesting_info_good = false;
        }
    }

    return harvesting_info_good;
}

bool ClusterDescriptor::verify_cluster_descriptor_info() {
    bool cluster_desc_info_good = true;

    cluster_desc_info_good &= verify_board_info_for_chips();

    cluster_desc_info_good &= verify_same_architecture();

    cluster_desc_info_good &= verify_harvesting_information();

    return cluster_desc_info_good;
}

uint8_t ClusterDescriptor::get_asic_location(ChipId chip_id) const {
    auto it = asic_locations.find(chip_id);
    if (it == asic_locations.end()) {
        return 0;
    }
    return it->second;
}

const std::unordered_map<ChipId, std::string> &ClusterDescriptor::get_chip_pci_bdfs() const { return chip_pci_bdfs; }

IODeviceType ClusterDescriptor::get_io_device_type() const { return io_device_type; }

uint16_t ClusterDescriptor::get_bus_id(ChipId chip_id) const {
    auto it = chip_to_bus_id.find(chip_id);
    if (it == chip_to_bus_id.end()) {
        return 0;
    }
    return it->second;
}

}  // namespace tt::umd
