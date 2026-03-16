// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "umd/device/types/arch.hpp"

namespace tt::umd {
/**
 * @brief Configuration options for controlling the behavior of the topology discovery process.
 *
 * This struct allows customization of how the UMD handles various scenarios
 * and potential issues encountered while discovering the hardware topology,
 * such as firmware mismatches or link training status.
 * The default initializers for these options match the expectations of tt-metal,
 * which requires strict checks and failing fast when a problem is discovered.
 */
struct TopologyDiscoveryOptions {
    /**
     * @brief Defines actions to take for selected issues during topology discovery.
     */
    enum class Action {
        THROW,   ///< Throw an exception and halt the discovery process.
        IGNORE,  ///< Log a warning message and continue the discovery process.
    };

    /**
     * @brief Action to take when a device's chip management firmware (CMFW) version does not match the expected
     * version. Defaults to THROW.
     */
    Action cmfw_mismatch_action = Action::THROW;

    /**
     * @brief Action to take when a device's chip management firmware (CMFW) version is unsupported by UMD.
     * Defaults to THROW.
     */
    Action cmfw_unsupported_action = Action::THROW;

    /**
     * @brief Action to take when the Ethernet firmware (ETH FW) version does not match the expected version across
     * devices. Defaults to THROW.
     */
    Action eth_fw_mismatch_action = Action::THROW;

    /**
     * @brief Action to take when an unexpected routing firmware configuration is detected.
     * Defaults to THROW.
     */
    Action unexpected_routing_firmware_config = Action::THROW;

    /**
     * @brief Action to take when Ethernet firmware heartbeat check fails.
     * The Ethernet firmware check is done on every ETH core on a device.
     * This means that Ethernet firmware on a particular core has crashed and cannot serve I/O.
     * If set to IGNORE, discovery from this core is skipped as it is certain not to be possible.
     * Defaults to THROW.
     */
    Action eth_fw_heartbeat_failure = Action::THROW;

    /**
     * @brief If true, the discovery process will attempt to find and include remote devices connected via Ethernet.
     * If false, only locally connected devices will be discovered.
     * Defaults to true.
     */
    bool discover_remote_devices = true;

    /**
     * @brief If true, the driver will wait for Ethernet link training to complete before proceeding.
     * This ensures that Ethernet links are fully operational, but can potentially slow down discovery.
     * Defaults to true.
     */
    bool wait_on_ethernet_link_training = true;

    /**
     * @brief If true, performs a hash check on the Ethernet firmware to ensure its integrity.
     * This is a more rigorous check than just comparing version numbers.
     * Defaults to false.
     */
    bool perform_eth_fw_hash_check = false;

    /**
     * @brief Controls how to determine the expect ETH FW version on Blackhole.
     * Blackhole does not provide ETH FW version in ARC telemetry.
     * This option has no effect on Wormhole.
     * Used only for tt-exalens tests that break ETH FW.
     * If set to true, the expected ETH FW version will be determined by observing the CMFW version.
     * If set to false, the expected ETH FW version will be determined by reading the ETH FW version
     * from the first observed ETH core during discovery. Defaults to false.
     * TODO: This option should be removed once ETH FW heartbeat checks are implemented, because
     * that will be used to check ETH core health instead of ETH FW version value.
     */
    bool predict_eth_fw_version_from_cmfw_version = false;

    /**
     * @brief If true, enables Ethernet link retraining on 6U machines when training fails.
     * When enabled, failed Ethernet links will be retrained up to a configured number of attempts.
     * Defaults to false.
     */
    bool perform_6u_eth_retrain = false;
};
}  // namespace tt::umd
