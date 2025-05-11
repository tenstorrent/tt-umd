// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <cxxopts.hpp>

#include "logger.hpp"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"

using namespace tt::umd;

int main(int argc, char *argv[]) {
    cxxopts::Options options("topology", "Extract system topology and save it to a yaml file.");

    options.add_options()("p,path", "File path to save cluster descriptor to.", cxxopts::value<std::string>())(
        "h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    std::string cluster_descriptor_path = "";
    if (result.count("path")) {
        cluster_descriptor_path = result["path"].as<std::string>();
    }

    std::string output_path = tt::umd::Cluster::create_cluster_descriptor()->serialize_to_file(cluster_descriptor_path);
    log_info(tt::LogSiliconDriver, "Cluster descriptor serialized to {}", output_path);
    return 0;
}
