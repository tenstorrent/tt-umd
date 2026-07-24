// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

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
     * @brief Action to take when device initialization fails. IGNORE keeps devices it can
     * and marks them as unhealthy.
     * Defaults to THROW.
     */
    Action device_init_failure_action = Action::THROW;

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
     * @brief If true, enables Ethernet link retraining on 6U machines when training fails.
     * When enabled, failed Ethernet links will be retrained up to a configured number of attempts.
     * Defaults to false.
     */
    bool perform_6u_eth_retrain = false;

    // Open devices in low-power mode. When true, devices are opened with O_APPEND
    // to opt out of KMD legacy mode (KMD >= 2.6.0), allowing idle power reduction.
    // Default is false (high-power / legacy mode) to preserve backward compatibility.
    bool low_power = false;

    /**
     * @brief If true, TTDevice instances created during discovery will use the safe API
     * (forwarded as the use_safe_api argument to TTDevice::create).
     * Defaults to false.
     */
    bool use_safe_api = false;

    /**
     * @brief If true, devices are opened exclusively (O_EXCL), taking exclusive ownership and
     * waiting (interruptibly) until any current owner releases the device. Matches the KMD
     * exclusive-open contract (tt-kmd#241) used by tools such as tt-flash for flash + reset.
     * Defaults to false.
     */
    bool open_devices_exclusively = false;
};
}  // namespace tt::umd
