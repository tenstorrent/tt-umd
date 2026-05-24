// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

/**
 * @defgroup tt_topology_discovery TopologyDiscovery
 * @{
 *
 * @brief Discovers all Tenstorrent devices in the system and builds the cluster topology.
 *
 * Enumerates local devices (PCIe or JTAG), then walks ethernet links to find
 * remote devices. Returns the full topology and initialized TTDevice instances
 * for every discovered device.
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref ClusterDescriptor | Topology graph of all discovered devices and their connections |
 * | @ref ChipId | Unique identifier for a device within the cluster |
 * | @ref TopologyDiscoveryOptions | Error handling policy and feature toggles for discovery |
 *
 */

class TopologyDiscovery {
public:
    /**
     * @brief Discovers all devices and builds the cluster topology.
     *
     * Enumerates local devices, then walks ethernet links to find remote devices.
     * Returns empty results if no devices are found.
     *
     * @param options Error handling policy and feature toggles for the discovery process.
     * @param io_device_type Transport used to reach local devices (PCIe or JTAG).
     * @param soc_descriptor_path Optional SoC descriptor file override.
     * @return @ref ClusterDescriptor and a map of @ref ChipId to @ref TTDevice for every discovered device.
     */
    static std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> discover(
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe,
        const std::string& soc_descriptor_path = "");

    virtual ~TopologyDiscovery() = default;
};

/** @} */  // end of tt_topology_discovery group

}  // namespace tt::umd
