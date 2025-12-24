// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/warm_reset.hpp"

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cxxopts.hpp>
#include <iostream>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "common.hpp"
#include "umd/device/topology/topology_discovery.hpp"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    cxxopts::Options options("warm_reset", "Perform warm reset on Tenstorrent devices.");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        log_info(tt::LogUMD, "Performing warm reset on all available devices...");
        WarmReset::warm_reset();
        log_info(tt::LogUMD, "Warm reset completed successfully. Running Topology discovery...");

        TopologyDiscovery::discover({});
        log_info(tt::LogUMD, "Topology discovery completed successfully.");
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error during warm reset: {}", e.what());
        return 1;
    }

    return 0;
}
