// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdint>
#include <cxxopts.hpp>
#include <ios>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

struct UbbId {
    std::uint32_t tray_id;
    std::uint32_t asic_id;
};

enum class ConnectorType { EXTERNAL, TRACE, LK1, LK2, LK3 };

enum class LinkingBoardType {
    A,
    B,
};

const std::unordered_map<tt::ARCH, std::vector<std::uint16_t>> ubb_bus_ids = {
    {tt::ARCH::WORMHOLE_B0, {0xC0, 0x80, 0x00, 0x40}},
    {tt::ARCH::BLACKHOLE, {0x00, 0x40, 0xC0, 0x80}},
};

const std::unordered_map<ConnectorType, LinkingBoardType> linking_board_types = {
    {ConnectorType::LK1, LinkingBoardType::A},
    {ConnectorType::LK2, LinkingBoardType::A},
    {ConnectorType::LK3, LinkingBoardType::B},
};

UbbId get_ubb_id(Cluster* cluster, const ChipId chip_id, const unsigned long  /*unique_chip_id*/) {
    const auto& tray_bus_ids = ubb_bus_ids.at(cluster->get_soc_descriptor(chip_id).arch);
    const auto bus_id = cluster->get_chip(chip_id)->get_tt_device()->get_pci_device()->get_device_info().pci_bus;
    auto tray_bus_id_it = std::find(tray_bus_ids.begin(), tray_bus_ids.end(), bus_id & 0xF0);
    if (tray_bus_id_it != tray_bus_ids.end()) {
        auto ubb_asic_id = bus_id & 0x0F;
        return UbbId{
            static_cast<uint32_t>(tray_bus_id_it - tray_bus_ids.begin() + 1), static_cast<uint32_t>(ubb_asic_id)};
    }
    return UbbId{0, 0};  // Invalid UBB ID if not found
}

bool check_if_external_cable_is_used(
    ClusterDescriptor* cluster_descriptor,
    const BoardType board_type,
    const ChipId chip_id,
    const unsigned long unique_chip_id,
    const int chan) {
    if (board_type == BoardType::UBB) {
        auto ubb_asic_id = ((unique_chip_id >> 56) & 0xFF);
        if (ubb_asic_id == 1) {
            // UBB 1 has external cables on channels 0-7.
            return (chan >= 0 and chan <= 7);
        } else if (ubb_asic_id >= 2 and ubb_asic_id <= 4) {
            // UBB 2 to 4 has external cables on channels 0-3.
            return (chan >= 0 and chan <= 3);
        } else if (ubb_asic_id == 5) {
            // UBB 5 has external cables on channels 4-7.
            return (chan >= 4 and chan <= 7);
        }
    } else if (board_type == BoardType::N300) {
        // N300 has external cables on channels 8-9 on MMIO chips and channels 0-1 on non-MMIO chips.
        auto mmio_device_id = cluster_descriptor->get_closest_mmio_capable_chip(chip_id);
        if (mmio_device_id == chip_id) {
            return (chan != 8 and chan != 9);
        } else {
            return (chan != 0 and chan != 1);
        }
    }
    return false;
}

ConnectorType get_connector_type(
    Cluster* cluster, BoardType board_type, ChipId chip_id, const unsigned long unique_chip_id, uint32_t chan) {
    if (check_if_external_cable_is_used(
            cluster->get_cluster_description(), board_type, chip_id, unique_chip_id, chan)) {
        return ConnectorType::EXTERNAL;
    }
    if (board_type == BoardType::UBB) {
        auto ubb_id = get_ubb_id(cluster, chip_id, unique_chip_id);
        if ((ubb_id.asic_id == 5 || ubb_id.asic_id == 6) && (12 <= chan && chan <= 15)) {
            return ConnectorType::LK1;
        } else if ((ubb_id.asic_id == 7 || ubb_id.asic_id == 8) && (12 <= chan && chan <= 15)) {
            return ConnectorType::LK2;
        } else if ((ubb_id.asic_id == 4 || ubb_id.asic_id == 8) && (8 <= chan && chan <= 11)) {
            return ConnectorType::LK3;
        } else {
            return ConnectorType::TRACE;
        }
    } else {
        return ConnectorType::TRACE;
    }
}

std::string get_ubb_id_str(Cluster* cluster, ChipId chip_id, const unsigned long unique_chip_id) {
    auto ubb_id = get_ubb_id(cluster, chip_id, unique_chip_id);
    return "Tray: " + std::to_string(ubb_id.tray_id) + " N" + std::to_string(ubb_id.asic_id);
}

std::string get_connector_str(
    Cluster* cluster, ChipId chip_id, const unsigned long unique_chip_id, uint32_t channel, BoardType board_type) {
    auto connector = get_connector_type(cluster, board_type, chip_id, unique_chip_id, channel);
    std::stringstream str;
    str << "(";
    switch (connector) {
        case ConnectorType::EXTERNAL:
            str << "external connector";
            break;
        case ConnectorType::TRACE:
            str << "internal trace";
            break;
        case ConnectorType::LK1:
            str << "LK1 trace";
            break;
        case ConnectorType::LK2:
            str << "LK2 trace";
            break;
        case ConnectorType::LK3:
            str << "LK3 trace";
            break;
    }
    str << ")";
    return str.str();
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("system_health", "A tool that reports system health.");

    options.add_options()("f,path", "File path to save cluster descriptor to.")("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    std::string cluster_descriptor_path;
    if (result.count("path")) {
        cluster_descriptor_path = result["path"].as<std::string>();
    }

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    auto cluster_descriptor = cluster->get_cluster_description();
    const auto& eth_connections = cluster_descriptor->get_ethernet_connections();
    auto unique_chip_ids = cluster_descriptor->get_chip_unique_ids();

    std::stringstream ss;
    std::stringstream chip_info_ss;
    std::vector<std::uint32_t> read_vec;

    if (unique_chip_ids.empty()) {
        // Temporary patch to workaround unique chip ids not being set for non-6U systems.
        for (const auto& chip_id : cluster->get_target_device_ids()) {
            unique_chip_ids[chip_id] = chip_id;
        }
    }

    ss << std::endl << "Found " << unique_chip_ids.size() << " chips in cluster_descriptor:" << std::endl;

    std::vector<std::string> unexpected_system_states;
    for (const auto& [chip_id, unique_chip_id] : unique_chip_ids) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        const auto& logical_coord = soc_desc.get_cores(CoreType::ETH, CoordSystem::LOGICAL);
        std::stringstream chip_id_ss;
        chip_id_ss << std::dec << "Chip: " << chip_id << " Unique ID: " << std::hex << unique_chip_id;
        auto board_type = cluster_descriptor->get_board_type(chip_id);
        if (board_type == BoardType::UBB) {
            auto [tray_id, ubb_asic_id] = get_ubb_id(cluster.get(), chip_id, unique_chip_id);
            chip_id_ss << " Tray: " << tray_id << " N" << ubb_asic_id;
        }
        ss << chip_id_ss.str() << std::endl;

        for (uint32_t chan = 0; chan < soc_desc.get_num_eth_channels(); chan++) {
            CoreCoord translated_coord = soc_desc.get_eth_core_for_channel(chan, CoordSystem::TRANSLATED);

            std::stringstream eth_ss;

            read_vec.resize(1);
            static constexpr std::uint32_t RETRAIN_COUNT_ADDR = 0x1EDC;  // wormhole
            cluster->read_from_device(read_vec.data(), chip_id, translated_coord, RETRAIN_COUNT_ADDR, sizeof(uint32_t));
            eth_ss << " eth channel " << std::dec << (uint32_t)chan << " " << logical_coord.at(chan).str();

            std::string connection_type = get_connector_str(cluster.get(), chip_id, unique_chip_id, chan, board_type);
            if (cluster_descriptor->ethernet_core_has_active_ethernet_link(chip_id, chan)) {
                if (eth_connections.at(chip_id).find(chan) != eth_connections.at(chip_id).end()) {
                    const auto& [connected_chip_id, connected_chan] =
                        cluster_descriptor->get_chip_and_channel_of_remote_ethernet_core(chip_id, chan);
                    const CoreCoord logical_eth_coord = CoreCoord(0, chan, CoreType::ETH, CoordSystem::LOGICAL);

                    chip_info_ss << "Connected chip: " << connected_chip_id
                                 << " connected eth core: " << logical_eth_coord.str() << std::endl;
                    eth_ss << " link UP " << connection_type << ", retrain: " << read_vec[0] << ", connected to chip "
                           << connected_chip_id << " " << logical_eth_coord.str();
                } else {
                    const auto& ethernet_connections_to_remote_cluster =
                        cluster_descriptor->get_ethernet_connections_to_remote_devices();
                    const auto& local_chip_id = chip_id;
                    const auto& local_connected_eth_core =
                        ethernet_connections_to_remote_cluster.at(local_chip_id).at(chan);

                    const auto& [connected_chip_unique_id, logical_eth_coord] = std::make_tuple(
                        std::get<0>(local_connected_eth_core),
                        soc_desc.get_eth_core_for_channel(std::get<1>(local_connected_eth_core), CoordSystem::LOGICAL));

                    chip_info_ss << "Connected unique chip: " << connected_chip_unique_id
                                 << " connected eth core: " << logical_eth_coord.str() << std::endl;
                    eth_ss << " link UP " << connection_type << ", retrain: " << read_vec[0] << ", connected to chip "
                           << connected_chip_unique_id << " " << logical_eth_coord.str();
                }

            } else {
                eth_ss << " link DOWN/unconnected " << connection_type;
            }

            ss << eth_ss.str() << std::endl;
        }
        ss << std::endl;
    }

    std::cout << chip_info_ss.str();
    std::cout << ss.str();

    std::string output_path = cluster_descriptor->serialize_to_file(cluster_descriptor_path);
    std::cout << "Cluster descriptor serialized to " << output_path << std::endl;

    return 0;
}
