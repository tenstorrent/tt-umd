// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cxxopts.hpp>
#include <exception>
#include <iostream>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/warm_reset_with_recovery.hpp"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    cxxopts::Options options(
        "warm_reset", "Perform warm reset on Tenstorrent devices. For reseting 6U, apply the --6u flag.");

    options.add_options()("6u", "Perform 6U warm reset.", cxxopts::value<bool>()->default_value("false"))(
        "max-attempts",
        "Maximum number of warm-reset + topology-discovery attempts. If discovery fails after "
        "a reset, another reset is performed. Default is 1 (no retry).",
        cxxopts::value<int>()->default_value("1"))("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    try {
        const bool is_6u_reset = result["6u"].as<bool>();
        const int max_attempts = result["max-attempts"].as<int>();

        log_info(
            tt::LogUMD,
            "Performing {} warm reset with up to {} attempt(s)...",
            is_6u_reset ? "6U" : "standard",
            max_attempts);

        const bool ok = is_6u_reset ? WarmResetWithRecovery::ubb_warm_reset(max_attempts)
                                    : WarmResetWithRecovery::warm_reset(max_attempts);

        if (!ok) {
            log_error(tt::LogUMD, "Warm reset failed after {} attempt(s). Exiting.", max_attempts);
            return 1;
        }

        log_info(tt::LogUMD, "Warm reset and topology discovery completed successfully.");
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Error during warm reset: {}", e.what());
        return 1;
    }

    return 0;
}
