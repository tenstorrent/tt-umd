// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <memory>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class TTDevice;
class ClusterDescriptor;

struct SimulationTopologyDiscoveryOptions {
    // The simulator build to host: a TTSim .so (when the path ends in .so) or an RTL simulator
    // directory. The backend is selected from the path, mirroring the simulation device factory.
    std::filesystem::path simulator_directory;
    // Connectivity/topology used to configure the simulator on the host path. Optional; when
    // attaching to a live host the topology comes from the host instead.
    std::shared_ptr<ClusterDescriptor> cluster_descriptor;
    int num_host_mem_channels = 0;
};

// Entry point for opening simulated devices, mirroring silicon TopologyDiscovery. It scans the
// well-known socket folder and decides, per device, whether to be the host or a client:
//   - no live socket  -> create a host device (the direct in-process backend / hot path) which
//                        binds + serves its socket;
//   - live socket     -> create a socket-backed client device that attaches to the host.
//
// Socket-first: binding the socket is the host-vs-client arbiter. The client/attach branch lands
// in a later PR; for now a live socket is reported as an error.
class SimulationTopologyDiscovery {
public:
    static std::map<ChipId, std::unique_ptr<TTDevice>> discover(const SimulationTopologyDiscoveryOptions& options);
};

}  // namespace tt::umd
