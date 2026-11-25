// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>

#include "tt-umd/types/communication_protocol.hpp"

namespace tt::umd {
/**
 * @brief Settings for discovering the chips a simulator models, rather than the host's own.
 *
 * A simulation backend has no OS device enumeration, so the topology comes from the simulator:
 * - A cluster_descriptor.yaml beside the simulator declares it. Discovery returns that descriptor
 *   and a device for each of its chips: MMIO-capable chips are opened on the simulator's
 *   endpoints, the others are reached over Ethernet through their closest MMIO-capable chip.
 *   Discovery throws error::RuntimeError when the descriptor gives a chip an architecture other
 *   than the simulator's. A chip's declared harvesting must be one its architecture allows, since its
 *   device is built from the declaration before it reports its own; otherwise discovery throws
 *   error::RuntimeError. cluster_id, discover_remote_devices and device_init_failure_action apply
 *   as to a walked topology: cluster_id replaces any id the file carries, discover_remote_devices =
 *   false leaves out the chips reached over Ethernet, and every device is initialized under
 *   device_init_failure_action. A chip whose closest MMIO-capable chip fails to initialize is not
 *   opened and is reported unhealthy. The walk's firmware-version and Ethernet checks are not run,
 *   so the descriptor carries no firmware versions. An initialized device's SoC descriptor follows
 *   what the device reports, not the file.
 * - Otherwise the image is asked which endpoints it exposes, a device is opened for each, and
 *   their Ethernet links are walked as on silicon.
 *
 * An empty TopologyDiscoveryOptions::soc_descriptor_path selects the soc_descriptor.yaml the
 * simulator ships beside it, or the architecture's built-in descriptor when it ships none.
 * io_device_type stays PCIe: a simulator models PCIe.
 */
struct SimulationDiscoveryOptions {
    /**
     * @brief Path to the simulator (a libttsim .so) whose chips discovery should walk.
     */
    std::string simulator_path;

    /**
     * @brief Number of host memory channels to give each simulated device.
     *
     * Unset, discovery sizes it from the topology it finds: the most chips that share one closest
     * MMIO-capable chip (that chip included), at most 4. Every simulated device of the discovered
     * cluster gets the same count.
     */
    std::optional<int> num_host_mem_channels;
};

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

    // Leave discovered devices in low-power mode. Blackhole devices are always opened power aware
    // (O_APPEND, KMD >= 2.6.0); unless this is set, discovery requests BUSY on each local device.
    // When true, Tensix may be clock gated and must not be accessed until set_power_state(BUSY).
    bool low_power = false;

    /**
     * @brief If true, TTDevice instances created during discovery will use the safe API
     * (forwarded as the use_safe_api argument to create_tt_device).
     * Defaults to false.
     */
    bool use_safe_api = false;

    /**
     * @brief Transport type used to reach local devices during discovery.
     * Defaults to PCIe.
     */
    IODeviceType io_device_type = IODeviceType::PCIe;

    /**
     * @brief Path to a custom SoC descriptor YAML file. If empty, the descriptor
     * is auto-detected from the hardware.
     */
    std::string soc_descriptor_path;

    /**
     * @brief Cluster id to stamp on the discovered cluster descriptor: a unique string identifying
     * the group of Tenstorrent accelerators attached to a common host / controller / root complex.
     * Defaults to the OS hostname, which is the right answer on bare metal. Callers running in a
     * container or a VM, where gethostname() names the container or the guest rather than the
     * accelerator group, have to supply one.
     *
     * Discovery throws when the supplied id is empty, longer than 128 characters, or contains
     * anything outside [A-Za-z0-9._-].
     */
    std::optional<std::string> cluster_id;

    /**
     * @brief Set to discover a simulator's chips instead of the host's; leave unset for silicon.
     *
     * The option's presence is the choice of backend, so a simulator can never be selected without
     * the settings it needs, nor those settings supplied for a host that will not read them.
     */
    std::optional<SimulationDiscoveryOptions> simulation;
};
}  // namespace tt::umd
