// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace tt::umd {

struct TopologyDiscoveryOptions {
    // Skip discovery of devices connected via Ethernet.
    bool no_remote_discovery = false;

    // Skip waiting for ETH core training.
    bool no_wait_for_eth_training = false;

    // Allow unsupported ETH firmware versions and do not fail when
    // cores have different ETH firmware versions.
    bool no_eth_firmware_strictness = false;

    // Predict ETH firmware version for entire cluster from the known
    // ETH firmware version bundled with the firmware bundle.
    bool predict_eth_fw_version = false;

    // Enables verifying ERISC FW on cores to ensure reliability of discovery.
    bool verify_eth_fw_hash = false;
};
}  // namespace tt::umd
