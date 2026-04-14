// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_umd/warm_reset.hpp"

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cxxopts.hpp>
#include <exception>
#include <iostream>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "common.hpp"
#include "tt_umd/topology/topology_discovery.hpp"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    cxxopts::Options options(
        "warm_reset", "Perform warm reset on Tenstorrent devices. For reseting 6U, apply the --6u flag.");

    options.add_options()("6u", "Perform 6U warm reset.", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    bool reset_result = false;

    try {
        const bool is_6u_reset = result["6u"].as<bool>();
        if (is_6u_reset) {
            log_info(tt::LogUMD, "Performing 6U warm reset...");
            reset_result = WarmReset::ubb_warm_reset();
        } else {
            log_info(tt::LogUMD, "Performing warm reset on all available devices...");
            reset_result = WarmReset::warm_reset();
        }

        if (reset_result) {
            log_info(tt::LogUMD, "Warm reset completed successfully. Running Topology discovery...");
        } else {
            log_error(tt::LogUMD, "Warm reset failed. Exiting.");
            return 1;
        }
        TopologyDiscovery::discover({});
        log_info(tt::LogUMD, "Topology discovery completed successfully.");
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error during warm reset: {}", e.what());
        return 1;
    }

    return 0;
}
