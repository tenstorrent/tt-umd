// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "api/umd/device/warm_reset_with_recovery.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/warm_reset.hpp"

namespace tt::umd {

namespace {

bool reset_then_discover(int max_attempts, const std::function<bool()>& do_reset) {
    max_attempts = std::max(max_attempts, 1);

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        log_info(tt::LogUMD, "Warm reset attempt {} of {}.", attempt, max_attempts);

        if (!do_reset()) {
            log_error(tt::LogUMD, "Warm reset failed on attempt {} of {}.", attempt, max_attempts);
            return false;
        }

        try {
            TopologyDiscovery::discover({});
            log_info(tt::LogUMD, "Topology discovery succeeded on attempt {}.", attempt);
            return true;
        } catch (const std::exception& e) {
            log_warning(
                tt::LogUMD, "Topology discovery failed on attempt {} of {}: {}", attempt, max_attempts, e.what());
        }
    }

    log_error(tt::LogUMD, "Warm reset with recovery exhausted all {} attempts.", max_attempts);
    return false;
}

}  // namespace

bool WarmResetWithRecovery::warm_reset(
    int max_attempts, bool reset_m3, bool secondary_bus_reset, std::chrono::milliseconds m3_delay) {
    return reset_then_discover(
        max_attempts, [&]() { return WarmReset::warm_reset({}, reset_m3, secondary_bus_reset, m3_delay); });
}

bool WarmResetWithRecovery::warm_reset_chip_id(
    const std::vector<int>& chip_ids,
    int max_attempts,
    bool reset_m3,
    bool secondary_bus_reset,
    std::chrono::milliseconds m3_delay) {
    return reset_then_discover(max_attempts, [&]() {
        return WarmReset::warm_reset_chip_id(chip_ids, reset_m3, secondary_bus_reset, m3_delay);
    });
}

bool WarmResetWithRecovery::warm_reset_pci_bdfs(
    const std::vector<std::string>& pci_bdfs,
    int max_attempts,
    bool reset_m3,
    bool secondary_bus_reset,
    std::chrono::milliseconds m3_delay) {
    return reset_then_discover(max_attempts, [&]() {
        return WarmReset::warm_reset_pci_bdfs(pci_bdfs, reset_m3, secondary_bus_reset, m3_delay);
    });
}

bool WarmResetWithRecovery::ubb_warm_reset(int max_attempts, std::chrono::milliseconds timeout_ms) {
    return reset_then_discover(max_attempts, [&]() { return WarmReset::ubb_warm_reset(timeout_ms); });
}

}  // namespace tt::umd
