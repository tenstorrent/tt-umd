// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <ostream>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>
#include <vector>

#include "common.hpp"
#include "tt-umd/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"

using namespace tt::umd;

int main(int argc, char *argv[]) {
    cxxopts::Options options("topology", "Extract system topology and save it to a yaml file.");

    options.add_options()("f,path", "File path to save cluster descriptor to.", cxxopts::value<std::string>())(
        "l,logical_devices",
        "List of logical device ids to filter cluster descriptor for.",
        cxxopts::value<std::vector<std::string>>())(
        "j,jtag",
        "Use JTAG mode for device communication. If not provided, PCIe will be used by default.",
        cxxopts::value<bool>()->default_value("false"))(
        "retrain_6u",
        "Attempt to retrain failed Ethernet links on 6U machines during topology discovery.",
        cxxopts::value<bool>()->default_value("false"))("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (result.count("logical_devices") && result.count("devices")) {
        std::cerr << "Error: Using both 'devices' and 'logical_devices' options is not allowed." << std::endl;
        return 1;
    }

    std::string cluster_descriptor_path;
    if (result.count("path")) {
        cluster_descriptor_path = result["path"].as<std::string>();
    }

    std::unordered_set<int> device_ids = {};
    IODeviceType device_type = IODeviceType::PCIe;

    if (result["jtag"].as<bool>()) {
        device_type = IODeviceType::JTAG;
    }

    TopologyDiscoveryOptions topology_options;
    topology_options.perform_6u_eth_retrain = result["retrain_6u"].as<bool>();

    std::unique_ptr<ClusterDescriptor> cluster_descriptor =
        Cluster::create_cluster_descriptor("", device_type, topology_options);

    if (result.count("logical_devices")) {
        std::unordered_set<int> logical_device_ids = extract_int_set(result["logical_devices"]);

        cluster_descriptor = ClusterDescriptor::create_constrained_cluster_descriptor(cluster_descriptor.get());
    }

    std::string output_path = cluster_descriptor->serialize_to_file(cluster_descriptor_path);
    log_info(tt::LogUMD, "Cluster descriptor serialized to {}", output_path);
    return 0;
}
