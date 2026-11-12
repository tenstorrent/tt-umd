// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
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

// TopologyDiscovery creates cluster descriptor after discovering all devices connected to the system.
class TopologyDiscovery {
public:
    static std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> discover(
        const TopologyDiscoveryOptions& options = {});

    virtual ~TopologyDiscovery() = default;
};

}  // namespace tt::umd
