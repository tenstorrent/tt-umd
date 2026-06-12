// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

// Wraps WarmReset entry points with a follow-up TopologyDiscovery::discover() and retries
// the whole sequence if discovery fails. This works around transient post-reset states
// (e.g. an ETH core that never recovers its heartbeat) which can leave the board in a
// state that only another reset can clear.
//
// Each method returns true if a (reset, discovery) attempt eventually succeeds, false
// otherwise. If the underlying warm reset itself fails, the methods bail immediately
// without retrying because such failures (no devices, ARM platform) do not recover by
// retrying.
class WarmResetWithRecovery {
public:
    static bool warm_reset(
        int max_attempts = 3,
        bool reset_m3 = false,
        bool secondary_bus_reset = true,
        std::chrono::milliseconds m3_delay = timeout::WARM_RESET_M3_TIMEOUT);

    static bool warm_reset_chip_id(
        const std::vector<int>& chip_ids = {},
        int max_attempts = 3,
        bool reset_m3 = false,
        bool secondary_bus_reset = true,
        std::chrono::milliseconds m3_delay = timeout::WARM_RESET_M3_TIMEOUT);

    static bool warm_reset_pci_bdfs(
        const std::vector<std::string>& pci_bdfs = {},
        int max_attempts = 3,
        bool reset_m3 = false,
        bool secondary_bus_reset = true,
        std::chrono::milliseconds m3_delay = timeout::WARM_RESET_M3_TIMEOUT);

    static bool ubb_warm_reset(
        int max_attempts = 3, std::chrono::milliseconds timeout_ms = timeout::UBB_WARM_RESET_TIMEOUT);
};

}  // namespace tt::umd
