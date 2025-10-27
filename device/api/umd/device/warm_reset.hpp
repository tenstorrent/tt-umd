/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class WarmReset {
public:
    static void warm_reset(std::vector<int> pci_device_ids = {}, bool reset_m3 = false);

    static void ubb_warm_reset(const std::chrono::milliseconds timeout_ms = timeout::UBB_WARM_RESET_TIMEOUT);

private:
    static constexpr auto POST_RESET_WAIT = std::chrono::milliseconds(2'000);
    static constexpr auto UBB_POST_RESET_WAIT = std::chrono::milliseconds(30'000);

    static void warm_reset_blackhole(std::vector<int> pci_device_ids);

    static void warm_reset_wormhole(std::vector<int> pci_device_ids, bool reset_m3);

    static void warm_reset_new(std::vector<int> pci_device_ids, bool reset_m3);

    static void wormhole_ubb_ipmi_reset(int ubb_num, int dev_num, int op_mode, int reset_time);

    static void ubb_wait_for_driver_load(const std::chrono::milliseconds timeout_ms);
};

}  // namespace tt::umd
