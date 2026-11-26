// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>
#include <utility>

#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

/**
 * Discover the simulator options.simulation names as having the topology declared_topology declares, the way
 * TopologyDiscovery::discover takes a cluster_descriptor.yaml shipped beside a TTSim image: each chip's architecture is
 * checked against the simulator's, the chips are selected by options.simulation->target_devices, and a device is
 * opened and initialized for each selected chip, MMIO-capable chips on the simulator's endpoints and the rest over
 * Ethernet through them.
 *
 * For the deprecated ClusterOptions::cluster_descriptor of a SIMULATION Cluster.
 */
std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>>
discover_declared_simulation_topology(
    const TopologyDiscoveryOptions& options, const ClusterDescriptor& declared_topology);

}  // namespace tt::umd
