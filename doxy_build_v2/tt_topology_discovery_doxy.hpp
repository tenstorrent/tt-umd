// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>
#include <utility>

#include "tt_enums_structs_constants_doxy.hpp"

namespace tt::umd {

class TTDevice;
class ClusterDescriptor;
struct TopologyDiscoveryOptions;

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
 * @client_cpp
 *
 */

/**
 * @brief Discovers devices and builds the cluster topology.
 *
 * @client_cpp
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
        const TopologyDiscoveryOptions& options);

    virtual ~TopologyDiscovery() = default;
};

/** @} */  // end of tt_topology_discovery group

}  // namespace tt::umd
