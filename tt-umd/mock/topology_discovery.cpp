// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery.hpp"

#include "tt-umd/cluster_descriptor.hpp"

namespace tt::umd {

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    devices[0] = TTDevice::create(0);
    auto cluster_desc = ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::WORMHOLE_B0, false);
    return {std::move(cluster_desc), std::move(devices)};
}

}  // namespace tt::umd
