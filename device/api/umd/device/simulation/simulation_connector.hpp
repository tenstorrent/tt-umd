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

struct SimulationConnectorOptions {
    // The simulator build to host: a TTSim .so (when the path ends in .so) or an RTL simulator
    // directory. The backend is selected from the path, mirroring the simulation device factory.
    std::filesystem::path simulator_directory;
    // Connectivity/topology used to configure the simulator on the host path. Optional; when
    // attaching to a live host the topology comes from the host instead.
    std::shared_ptr<ClusterDescriptor> cluster_descriptor;
    int num_host_mem_channels = 0;
};

// Entry point for opening simulated devices, mirroring silicon TopologyDiscovery. The role is
// decided purely from what simulator_directory points at -- no socket bind-race:
//   - a ".so" file            -> host running the TTSim backend; binds + serves its socket;
//   - a directory of per-chip -> client: one socket-backed device per socket in the directory,
//     simulation sockets          each attaching to a live host and sourcing its SoC descriptor
//                                 (and backend kind) from that host over the wire;
//   - any other directory     -> host running the RTL backend from that build directory.
class SimulationConnector {
public:
    // Host vs client, decided from simulator_directory (the two host backends collapse to Host).
    // Exposed so callers (e.g. Cluster) can branch on the role without duplicating the path logic.
    enum class Role { Host, Client };
    static Role role_for(const std::filesystem::path& simulator_directory);

    static std::map<ChipId, std::unique_ptr<TTDevice>> discover(const SimulationConnectorOptions& options);
};

}  // namespace tt::umd
