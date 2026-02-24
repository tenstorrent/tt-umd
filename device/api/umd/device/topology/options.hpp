// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "umd/device/types/arch.hpp"

namespace tt::umd {

struct TopologyDiscoveryOptions {
    enum class Action { THROW, IGNORE };

    std::optional<tt::ARCH> preferred_architecture = std::nullopt;

    uint8_t noc_id = 0;

    Action failed_init_action = Action::THROW;

    Action channel_failure_action = Action::THROW;

    Action cmfw_mismatch_action = Action::THROW;

    Action cmfw_unsupported_action = Action::THROW;

    Action eth_fw_mismatch_action = Action::THROW;

    Action unexpected_routing_firmware_config = Action::THROW;

    bool discover_remote_devices = true;

    bool wait_on_ethernet_link_training = true;

    bool perform_eth_fw_hash_check = false;

    bool expect_matching_eth_fw_version = false;
};

constexpr TopologyDiscoveryOptions DEBUG_DEFAULT_OPTIONS = {
    .failed_init_action = TopologyDiscoveryOptions::Action::IGNORE,
    .eth_fw_mismatch_action = TopologyDiscoveryOptions::Action::IGNORE,
    .unexpected_routing_firmware_config = TopologyDiscoveryOptions::Action::IGNORE,
    .wait_on_ethernet_link_training = false,
    .expect_matching_eth_fw_version = true};
}  // namespace tt::umd
