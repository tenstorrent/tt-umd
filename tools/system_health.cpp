// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <cxxopts.hpp>
#include <tt-logger/tt-logger.hpp>

#include "common.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/tt_soc_descriptor.h"

using namespace tt::umd;

const std::unordered_map<tt::ARCH, std::vector<std::uint16_t>> ubb_bus_ids = {
    {tt::ARCH::WORMHOLE_B0, {0x00, 0x40, 0xC0, 0x80}},
    {tt::ARCH::BLACKHOLE, {0xC0, 0x80, 0x00, 0x40}},
};

std::pair<std::uint32_t, std::uint32_t> get_ubb_ids(
    Cluster* cluster, const chip_id_t chip_id, const unsigned long unique_chip_id) {
    const auto& tray_bus_ids = ubb_bus_ids.at(cluster->get_soc_descriptor(chip_id).arch);
    auto tray_bus_id_it = std::find(
        tray_bus_ids.begin(),
        tray_bus_ids.end(),
        cluster->get_chip(chip_id)->get_tt_device()->get_pci_device()->get_device_info().pci_bus & 0xF0);
    if (tray_bus_id_it != tray_bus_ids.end()) {
        // same as get_ubb_asic_id in tt-metal
        auto ubb_asic_id = ((unique_chip_id >> 56) & 0xFF);
        return std::make_pair(tray_bus_id_it - tray_bus_ids.begin() + 1, ubb_asic_id);
    }
    return std::make_pair(0, 0);
}

bool check_if_external_cable_is_used(
    tt_ClusterDescriptor* cluster_descriptor,
    const BoardType board_type,
    const int chip_id,
    const unsigned long unique_chip_id,
    const int chan) {
    if (board_type == BoardType::UBB) {
        auto ubb_asic_id = ((unique_chip_id >> 56) & 0xFF);
        if (ubb_asic_id == 1) {
            // UBB 1 has external cables on channels 0-7
            return (chan >= 0 and chan <= 7);
        } else if (ubb_asic_id >= 2 and ubb_asic_id <= 4) {
            // UBB 2 to 4 has external cables on channels 0-3
            return (chan >= 0 and chan <= 3);
        } else if (ubb_asic_id == 5) {
            // UBB 5 has external cables on channels 4-7
            return (chan >= 4 and chan <= 7);
        }
    } else if (board_type == BoardType::N300) {
        // N300 has external cables on channels 8-9 on MMIO chips and channels 0-1 on non-MMIO chips
        auto mmio_device_id = cluster_descriptor->get_closest_mmio_capable_chip(chip_id);
        if (mmio_device_id == chip_id) {
            return (chan != 8 and chan != 9);
        } else {
            return (chan != 0 and chan != 1);
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("system_health", "A tool that reports system health.");

    options.add_options()("f,path", "File path to save cluster descriptor to.");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    std::string cluster_descriptor_path = "";
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
        // Temporary patch to workaround unique chip ids not being set for non-6U systems
        for (const auto& chip_id : cluster->get_target_device_ids()) {
            unique_chip_ids[chip_id] = chip_id;
        }
    }

    ss << std::endl << "Found " << unique_chip_ids.size() << " chips in cluster_descriptor:" << std::endl;

    std::vector<std::string> unexpected_system_states;
    for (const auto& [chip_id, unique_chip_id] : unique_chip_ids) {
        const tt_SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        const auto& logical_coord = soc_desc.get_cores(CoreType::ETH, CoordSystem::LOGICAL);
        std::stringstream chip_id_ss;
        chip_id_ss << std::dec << "Chip: " << chip_id << " Unique ID: " << std::hex << unique_chip_id;
        auto board_type = cluster_descriptor->get_board_type(chip_id);
        if (board_type == BoardType::GALAXY) {
            auto [tray_id, ubb_asic_id] = get_ubb_ids(cluster.get(), chip_id, unique_chip_id);
            chip_id_ss << " Tray: " << tray_id << " N" << ubb_asic_id;
        }
        ss << chip_id_ss.str() << std::endl;

        for (auto chan = 0; chan < soc_desc.get_num_eth_channels(); chan++) {
            CoreCoord translated_coord = soc_desc.get_eth_core_for_channel(chan, CoordSystem::TRANSLATED);

            std::stringstream eth_ss;

            read_vec.resize(sizeof(uint32_t) / sizeof(uint32_t));
            static constexpr std::uint32_t RETRAIN_COUNT_ADDR = 0x1EDC;  // wormhole
            cluster->read_from_device(read_vec.data(), chip_id, translated_coord, RETRAIN_COUNT_ADDR, sizeof(uint32_t));
            eth_ss << " eth channel " << std::dec << (uint32_t)chan << " " << logical_coord.at(chan).str();

            const bool is_external_cable =
                check_if_external_cable_is_used(cluster_descriptor, board_type, chip_id, unique_chip_id, chan);

            std::string connection_type = is_external_cable ? "(external connector)" : "(internal trace)";

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
                        cluster_descriptor->get_ethernet_connections_to_remote_mmio_devices();
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
