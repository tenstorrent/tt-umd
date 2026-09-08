// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class TTDevice;
class ClusterDescriptor;

struct SimulationConnectorOptions {
    // The simulator build to host: a TTSim .so (when the path ends in .so) or an RTL simulator
    // directory. The backend is selected from the path, mirroring the simulation device factory.
    std::filesystem::path simulator_directory;
    // Host path only: publish each simulated chip over a per-chip socket so other processes can
    // attach as clients. Disabled by default so a direct discover() call yields private in-process
    // devices (the hot path) -- opt in only for a shared-simulation/server workflow. Ignored on the
    // client path (attaching to a live host is always over sockets).
    bool serve_over_sockets = false;
    // Host path only: the directory this server serves its per-chip sockets in (when
    // serve_over_sockets is set). Empty means allocate a fresh one (allocate_server_directory), so
    // distinct hosts never collide; set it to serve in a specific directory (e.g. one the caller
    // pre-allocated to report to the user).
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

    // What a discover() call actually opened: which side of the connection this process ended up
    // on, and what simulator sits behind it. Reported alongside the devices because none of it is
    // recoverable afterwards -- re-classifying the path answers only the role, and races a host
    // that has since exited, while a server directory discover() allocated itself is otherwise
    // never told to the caller at all.
    struct Connection {
        Role role = Role::Host;
        // The simulator behind the devices: a libttsim .so path or an RTL build directory. As a
        // host, the build this process opened; as a client, the build the host reports it runs --
        // empty when talking to a host that predates serving that field.
        std::filesystem::path simulator;
        // Which kind of simulator that is, and the architecture it simulates.
        SimulationBackendType backend = SimulationBackendType::TTSIM;
        tt::ARCH arch = tt::ARCH::Invalid;
        // As a host, the directory this process serves its sockets in: empty when serving is off,
        // and the freshly allocated directory when the caller left server_directory empty. As a
        // client, the directory it attached to.
        std::filesystem::path server_directory;
        // The per-chip sockets in that directory ({chip_id -> socket path}). Empty for a host that
        // is not serving.
        std::map<ChipId, std::filesystem::path> sockets;
    };

    // The devices discover() opened, and the connection it opened them over.
    struct Result {
        Connection connection;
        std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    };

    static Result discover(const SimulationConnectorOptions& options);

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
