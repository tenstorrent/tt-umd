// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class TTDevice;
class ClusterDescriptor;

struct SimulationConnectorOptions {
    // The simulator build to host: a TTSim .so (when the path ends in .so) or an RTL simulator
    // directory. The backend is selected from the path, mirroring the simulation device factory.
    std::filesystem::path simulator_directory;
    // Host path only: serve the opened devices with SimulationConnector::serve, so other processes
    // can attach as clients; the Server comes back in Result::server. Disabled by default so a
    // direct discover() call yields private in-process devices (the hot path) -- opt in only for a
    // shared-simulation/server workflow. Ignored on the client path (attaching to a live host is
    // always over sockets).
    bool serve_over_sockets = false;
    // Host path only: the server directory passed to serve() when serve_over_sockets is set. Empty
    // means claim a fresh one.
    std::filesystem::path server_directory;
    // Not used: the simulator is not configured from it, and a host reports (and serves) the
    // simulator build's own topology, so a host logs a warning when it is set. A client takes the
    // topology from the host.
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

// Entry point for opening simulated devices, mirroring silicon TopologyDiscovery, and for serving
// devices to other processes over per-chip sockets (serve). The role discover() takes is decided
// purely from what simulator_directory points at -- no socket bind-race:
//   - a ".so" file            -> host running the TTSim backend (served when serve_over_sockets
//                                 is set);
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

    // Devices served to other processes by serve(): one per-chip socket in one server directory.
    // Serving starts before serve() returns and lasts until the Server is destroyed; destroying it
    // stops accepting, closes every client connection, waits for the requests in flight and removes
    // the socket files, and then the server directory too when it is one allocate_server_directory
    // claims and nothing else is left in it.
    class Server {
    public:
        ~Server();

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        // The directory the sockets are in: the one passed to serve(), or the one it claimed.
        const std::filesystem::path& server_directory() const;

        // The per-chip sockets served ({chip_id -> socket path}), one for each device served.
        const std::map<ChipId, std::filesystem::path>& sockets() const;

    private:
        friend class SimulationConnector;

        struct Impl;

        explicit Server(std::unique_ptr<Impl> impl);

        std::unique_ptr<Impl> impl_;
    };

    // The devices discover() opened, the connection it opened them over, and the topology they
    // sit in.
    struct Result {
        Connection connection;
        std::map<ChipId, std::unique_ptr<TTDevice>> devices;
        // The cluster the opened devices belong to: on a host, the simulator build's own topology,
        // or -- when the build ships none -- a mock over exactly the chips it opened; this is also
        // what the host serves. On a client, the topology its host serves. Never null.
        std::shared_ptr<ClusterDescriptor> cluster_descriptor;
        // The Server serving the devices, when the host was asked to serve_over_sockets; null
        // otherwise. Declared after devices so a Result stops serving before its devices go; a
        // caller that moves the devices out must destroy this before them.
        std::unique_ptr<Server> server;
    };

    static Result discover(const SimulationConnectorOptions& options);

    // Serves devices to other processes, one per-chip socket each in one server directory, so that
    // a client (TopologyDiscovery or discover() pointed at that directory) can attach to them. Any
    // TTDevice can be served -- simulated, silicon, or reached through another chip -- and the
    // devices stay usable by the caller while they are served.
    //
    // A client's requests reach each device through its public TTDevice interface:
    //   - READ / WRITE: read_from_device / write_to_device at the coordinate the client sends,
    //     which the client has already translated, so it is used as is (CoordSystem::LITERAL), on
    //     the default NOC. The requests to one device are made one at a time; whatever else uses
    //     the device meanwhile relies on the device's own thread safety. A failed access answers
    //     that request with a nonzero status and serving goes on.
    //   - GET_DEVICE_INFO: the device's SoC descriptor (architecture, harvesting, NOC translation,
    //     and the descriptor's YAML when it was built from a file) and which simulator backs it
    //     (TTDevice::get_simulation_backend), with that simulator's path when the device runs on
    //     it directly rather than through another chip. A device no simulator backs is described
    //     as a TTSim device with an empty simulator path, the wire protocol having no other kind.
    //   - GET_CLUSTER_DESCRIPTOR: cluster_descriptor, as it was when serve() was called.
    //   - SHUTDOWN: calls shutdown_handler, then acknowledges; without a handler it only
    //     acknowledges, so a stray client cannot stop a server that did not opt in. The handler runs
    //     on a serving thread and can run several times, concurrently: it must only signal the
    //     owner (set a flag, notify a condition variable) and never destroy the Server itself, whose
    //     destruction waits for that thread.
    //
    // @param devices The devices to serve, keyed by the chip id of cluster_descriptor each one is;
    //        not owned. Each must stay alive until the Server is destroyed. A chip of
    //        cluster_descriptor left out gets no socket, and a client that attaches to the whole
    //        topology then refuses it.
    // @param cluster_descriptor The topology served to clients.
    // @param server_directory An existing directory to serve in; empty claims a fresh one
    //        (allocate_server_directory).
    // @param shutdown_handler What a client's SHUTDOWN request asks the owner to do; may be empty.
    // @throws error::RuntimeError when devices is empty or holds a null device, when a chip id is
    //         not a chip of cluster_descriptor, or when a live server already serves one of the
    //         chips in server_directory. Nothing is served then.
    static std::unique_ptr<Server> serve(
        const std::map<ChipId, TTDevice*>& devices,
        const ClusterDescriptor& cluster_descriptor,
        const std::filesystem::path& server_directory = {},
        std::function<void()> shutdown_handler = {});

    // Claims a fresh directory for one simulation server (the lowest free index) and returns it, so
    // a caller can report the location before it starts serving there (pass it back to serve()).
    // The claim is atomic, so racing starts get distinct directories.
    static std::filesystem::path allocate_server_directory();

    // One pass over every server directory: the servers still open, and the ones whose host was
    // gone, which this removed. Both come from a single scan, so a caller acting on both sees one
    // consistent view. A directory with no socket yet is never removed -- it cannot be told apart
    // from a host still coming up.
    struct ServerScan {
        std::vector<SimulationServerInfo> live;
        std::vector<SimulationServerInfo> removed;
    };

    static ServerScan scan_servers();

    // The servers currently open on this machine, ordered by index. Opens no devices. Exposed for
    // management tooling (list / kill).
    static std::vector<SimulationServerInfo> list_servers();

    // Removes what hosts that are gone left on disk, and returns those servers.
    static std::vector<SimulationServerInfo> prune_dead_servers();
};

}  // namespace tt::umd
