// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class TTDevice;
class ClusterDescriptor;

struct SimulationConnectorOptions {
    // The simulator build to host: a TTSim .so (when the path ends in .so) or an RTL simulator
    // directory. The backend is selected from the path, mirroring the simulation device factory.
    std::filesystem::path simulator_directory;
    // Host path only: the directory this server serves its per-chip sockets in. Empty means
    // allocate a fresh one (allocate_server_directory), so distinct hosts never collide; set it to
    // serve in a specific directory (e.g. one the caller pre-allocated to report to the user).
    std::filesystem::path server_directory;
    // Connectivity/topology used to configure the simulator on the host path. Optional; when
    // attaching to a live host the topology comes from the host instead.
    std::shared_ptr<ClusterDescriptor> cluster_descriptor;
    int num_host_mem_channels = 0;
};

// One simulation server open on this machine, as seen by list_servers(): its index, the directory
// it serves in, and the per-chip sockets present there ({chip_id -> socket path}).
struct SimulationServerInfo {
    int index = 0;
    std::filesystem::path directory;
    std::map<ChipId, std::filesystem::path> sockets;
};

// Entry point for opening simulated devices, mirroring silicon TopologyDiscovery. The role is
// decided purely from what simulator_directory points at -- no socket bind-race:
//   - a ".so" file            -> host running the TTSim backend; serves its socket in a fresh
//                                 server directory;
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

    // Claims a fresh directory for one simulation server (the lowest free index) and returns it, so
    // a caller can report the location before it starts serving there (pass it back as
    // SimulationConnectorOptions::server_directory or ClusterOptions::simulator_server_directory).
    // The claim is atomic, so racing starts get distinct directories.
    static std::filesystem::path allocate_server_directory();

    // The simulation servers currently open on this machine, ordered by index, discovered by
    // scanning the well-known server directories (the same directories a client attaches to). Does
    // not connect to them. Exposed for management tooling (list / kill) without opening devices.
    static std::vector<SimulationServerInfo> list_servers();
};

}  // namespace tt::umd
