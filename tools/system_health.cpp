// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <cxxopts.hpp>
#include <tt-logger/tt-logger.hpp>

#include "common.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    cxxopts::Options options("system_health", "<Give explanation here>.");

    // // Work on the comments later on
    options.add_options()("f,path", "File path to save cluster descriptor to.", cxxopts::value<std::string>())(
        "l,logical_devices",
        "List of logical device ids to filter cluster descriptor for.",
        cxxopts::value<std::vector<std::string>>())(
        "p,pci_devices",
        "List of pci device ids to perform topology discovery on.",
        cxxopts::value<std::vector<std::string>>())("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (result.count("logical_devices") && result.count("pci_devices")) {
        std::cerr << "Error: Using both 'pci_devices' and 'logical_devices' options is not allowed." << std::endl;
        return 1;
    }

    std::string cluster_descriptor_path = "";
    if (result.count("path")) {
        cluster_descriptor_path = result["path"].as<std::string>();
    }

    std::unordered_set<int> pci_ids = {};
    if (result.count("pci_devices")) {
        pci_ids = extract_int_set(result["pci_devices"]);
    }

    std::unique_ptr<tt_ClusterDescriptor> cluster_descriptor = tt::umd::Cluster::create_cluster_descriptor("", pci_ids);
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const auto& eth_connections = cluster_descriptor->get_ethernet_connections();
    const auto& eth_connections_to_remote_mmio_devices =
        cluster_descriptor->get_ethernet_connections_to_remote_mmio_devices();
    auto unique_chip_ids = cluster_descriptor->get_chip_unique_ids();

    std::stringstream ss;
    std::vector<std::uint32_t> read_vec;

    if (unique_chip_ids.empty()) {
        // Temporary patch to workaround unique chip ids not being set for non-6U systems
        for (const auto& chip_id : cluster->get_target_device_ids()) {
            unique_chip_ids[chip_id] = chip_id;
        }
    }
    ss << "Found " << unique_chip_ids.size() << " chips in cluster_descriptor:" << std::endl;

    std::vector<std::string> unexpected_system_states;
    for (const auto& [chip_id, unique_chip_id] : unique_chip_ids) {
        const tt_SocDescriptor& soc_desc = cluster->get_chip(chip_id)->get_soc_descriptor();
        const auto& logical_coord = soc_desc.get_cores(CoreType::ETH, CoordSystem::LOGICAL);
        std::stringstream chip_id_ss;
        chip_id_ss << std::dec << "Chip: " << chip_id << " Unique ID: " << std::hex << unique_chip_id;
        auto board_type = cluster_descriptor->get_board_type(chip_id);
        if (board_type == BoardType::GALAXY) {
            // auto [tray_id, ubb_asic_id] = get_ubb_ids(chip_id);
            // chip_id_ss << " Tray: " << tray_id << " N" << ubb_asic_id;
        }
        ss << chip_id_ss.str() << std::endl;
        std::map<CoreCoord, int> logical_eth_core_to_chan_map;
        for (const auto& logical_coordinates : logical_coord) {
            logical_eth_core_to_chan_map.insert(
                {{logical_coordinates.x, logical_coordinates.y, CoreType::ETH, CoordSystem::LOGICAL},
                 logical_coordinates.y});
        }
        for (const auto& [eth_core, chan] : logical_eth_core_to_chan_map) {
            CoreCoord translated_coord =
                soc_desc.translate_coord_to({eth_core, CoreType::ETH, CoordSystem::LOGICAL}, CoordSystem::TRANSLATED);

            tt_cxy_pair virtual_eth_core(chip_id, translated_coord);
            std::stringstream eth_ss;

            read_vec.resize(sizeof(uint32_t) / sizeof(uint32_t));
            static constexpr std::uint32_t RETRAIN_COUNT_ADDR = 0x1EDC;  // wormhole
            cluster->read_from_device(
                read_vec.data(), virtual_eth_core.chip, translated_coord, RETRAIN_COUNT_ADDR, sizeof(uint32_t));
            eth_ss << " eth channel " << std::dec << (uint32_t)chan << " " << eth_core.str();

            const bool is_external_cable = [board_type, &cluster_descriptor](
                                               const int chip_id, const unsigned long unique_chip_id, const int chan) {
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
            }(chip_id, unique_chip_id, chan);

            std::string connection_type = is_external_cable ? "(external connector)" : "(internal trace)";

            if (cluster_descriptor->ethernet_core_has_active_ethernet_link(chip_id, chan)) {
                const auto& eth_connections = cluster_descriptor->get_ethernet_connections();
                if (eth_connections.at(chip_id).find(chan) != eth_connections.at(chip_id).end()) {
                    auto connected_eth_core_local =
                        cluster_descriptor->get_chip_and_channel_of_remote_ethernet_core(chip_id, chan);
                    const CoreCoord logical_eth_coord = CoreCoord(0, chan, CoreType::ETH, CoordSystem::LOGICAL);

                    const auto& [connected_chip_id, connected_eth_core] =
                        std::make_tuple(std::get<0>(connected_eth_core_local), logical_eth_coord);

                    std::cout << "Connected chip: " << connected_chip_id
                              << " connected eth core: " << connected_eth_core.str() << std::endl;
                    eth_ss << " link UP " << connection_type << ", retrain: " << read_vec[0] << ", connected to chip "
                           << connected_chip_id << " " << connected_eth_core.str();
                } else {
                    const auto& ethernet_connections_to_remote_cluster =
                        cluster_descriptor->get_ethernet_connections_to_remote_mmio_devices();
                    const auto& local_chip_id = chip_id;
                    const auto& local_eth_core = eth_core;
                    const auto& local_connected_eth_core =
                        ethernet_connections_to_remote_cluster.at(local_chip_id).at(chan);

                    const auto& [connected_chip_unique_id, connected_eth_core] = std::make_tuple(
                        std::get<0>(local_connected_eth_core),
                        soc_desc.get_eth_core_for_channel(std::get<1>(local_connected_eth_core), CoordSystem::LOGICAL));

                    std::cout << "Connected unique chip: " << connected_chip_unique_id
                              << " connected eth core: " << connected_eth_core.str() << std::endl;
                    eth_ss << " link UP " << connection_type << ", retrain: " << read_vec[0] << ", connected to chip "
                           << connected_chip_unique_id << " " << connected_eth_core.str();
                }

                if (read_vec[0] > 0) {
                    unexpected_system_states.push_back(chip_id_ss.str() + eth_ss.str());
                }
            } else {
                eth_ss << " link DOWN/unconnected " << connection_type;
                unexpected_system_states.push_back(chip_id_ss.str() + eth_ss.str());
            }

            ss << eth_ss.str() << std::endl;
        }
        ss << std::endl;
    }

    log_info(tt::LogTest, "{}", ss.str());

    // Print a summary of unexpected system states
    for (const auto& err_str : unexpected_system_states) {
        log_warning(tt::LogTest, "{}", err_str);
    }

    if (result.count("logical_devices")) {
        std::unordered_set<int> logical_device_ids = extract_int_set(result["logical_devices"]);

        cluster_descriptor =
            tt_ClusterDescriptor::create_constrained_cluster_descriptor(cluster_descriptor.get(), logical_device_ids);
    }

    std::string output_path = cluster_descriptor->serialize_to_file(cluster_descriptor_path);
    log_info(tt::LogSiliconDriver, "Cluster descriptor serialized to {}", output_path);
    return 0;
}
