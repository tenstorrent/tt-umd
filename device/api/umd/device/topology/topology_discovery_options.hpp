// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "umd/device/types/arch.hpp"

namespace tt::umd {

struct TopologyDiscoveryOptions {
    enum class Action { THROW, WARN };
    enum class DeviceAction { THROW, SKIP, KEEP };

    DeviceAction cmfw_mismatch_action = DeviceAction::THROW;

    DeviceAction cmfw_unsupported_action = DeviceAction::THROW;

    Action eth_fw_mismatch_action = Action::THROW;

    Action unexpected_routing_firmware_config = Action::THROW;

    bool discover_remote_devices = true;

    bool wait_on_ethernet_link_training = true;

    bool perform_eth_fw_hash_check = false;

    bool expect_matching_eth_fw_version = false;
};
}  // namespace tt::umd
