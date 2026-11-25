// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/tt_device/tt_device.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt;
using namespace tt::umd;

namespace {

void bind_simulation_connector(nb::module_ &m) {
    nb::class_<SimulationConnectorOptions>(m, "SimulationConnectorOptions")
        .def(nb::init<>())
        .def_rw(
            "simulator_directory",
            &SimulationConnectorOptions::simulator_directory,
            "The simulator build to host (a TTSim .so or an RTL simulator directory), or the directory a running "
            "server keeps its sockets in to attach as a client.")
        .def_rw(
            "serve_over_sockets",
            &SimulationConnectorOptions::serve_over_sockets,
            "Host path only: serve the opened devices with SimulationConnector.serve so other processes can attach "
            "as clients; they are served until every device discover() returned is dropped. Off by default, so "
            "discover() yields private in-process devices.")
        .def_rw(
            "server_directory",
            &SimulationConnectorOptions::server_directory,
            "Host path only: the server directory passed to serve() when serve_over_sockets is set. Empty means claim "
            "a fresh one.")
        .def_rw(
            "cluster_descriptor",
            &SimulationConnectorOptions::cluster_descriptor,
            "Not used: a host reports and serves the simulator's own topology (and warns when this is set); a "
            "client takes the topology from the host.")
        .def_rw("num_host_mem_channels", &SimulationConnectorOptions::num_host_mem_channels);

    nb::class_<SimulationServerInfo>(m, "SimulationServerInfo")
        .def_ro("index", &SimulationServerInfo::index, "The server's index, as used by the sim_server tool.")
        .def_ro("directory", &SimulationServerInfo::directory, "The directory this server serves its sockets in.")
        .def_ro(
            "sockets", &SimulationServerInfo::sockets, "Per-chip sockets present in the directory: chip id -> path.")
        .def("__repr__", [](const SimulationServerInfo &self) {
            return fmt::format(
                "SimulationServerInfo(index={}, directory='{}', chips={})",
                self.index,
                self.directory.string(),
                self.sockets.size());
        });

    nb::class_<SimulationConnector> simulation_connector(
        m,
        "SimulationConnector",
        "Entry point for opening simulated devices, mirroring silicon TopologyDiscovery. The role is decided purely "
        "from what simulator_directory points at. Bound in every build: when UMD was built without simulation "
        "support, discover() and serve() raise error.UmdBaseException, while the server-directory calls "
        "(role_for, allocate_server_directory, scan_servers, list_servers, prune_dead_servers) work.");

    nb::enum_<SimulationConnector::Role>(simulation_connector, "Role")
        .value("HOST", SimulationConnector::Role::Host, "This process runs the simulation.")
        .value("CLIENT", SimulationConnector::Role::Client, "This process attaches to a simulation another host runs.");

    nb::class_<SimulationConnector::Connection>(
        simulation_connector,
        "Connection",
        "What a discover() call opened: the role this process took and the simulator behind it.")
        .def_ro(
            "role",
            &SimulationConnector::Connection::role,
            "Whether this process hosts the simulation or attached to one.")
        .def_ro(
            "simulator",
            &SimulationConnector::Connection::simulator,
            "The simulator behind the devices: a libttsim .so path or an RTL build directory. Empty when attached to "
            "a host that predates reporting it.")
        .def_ro("backend", &SimulationConnector::Connection::backend, "Which kind of simulator that is.")
        .def_ro("arch", &SimulationConnector::Connection::arch, "The architecture it simulates.")
        .def_ro(
            "server_directory",
            &SimulationConnector::Connection::server_directory,
            "As a host, the directory it serves its sockets in -- empty when serving is off. As a client, the "
            "directory it attached to.")
        .def_ro(
            "sockets",
            &SimulationConnector::Connection::sockets,
            "The per-chip sockets in that directory: chip id -> path.")
        .def("__repr__", [](const SimulationConnector::Connection &self) {
            return fmt::format(
                "SimulationConnection(role={}, simulator='{}', arch={}, server_directory='{}')",
                self.role == SimulationConnector::Role::Host ? "HOST" : "CLIENT",
                self.simulator.string(),
                tt::arch_to_str(self.arch),
                self.server_directory.string());
        });

    nb::class_<SimulationConnector::Server>(
        simulation_connector,
        "Server",
        "Devices served to other processes by SimulationConnector.serve(): one per-chip socket in one server "
        "directory. Serves until it is dropped; dropping it stops accepting, closes every client connection, waits "
        "for the requests in flight and removes the socket files, and then the server directory too when it is one "
        "allocate_server_directory claims and nothing else is left in it.")
        .def(
            "server_directory",
            &SimulationConnector::Server::server_directory,
            "The directory the sockets are in: the one passed to serve(), or the one it claimed.")
        .def(
            "sockets",
            &SimulationConnector::Server::sockets,
            "The per-chip sockets served: chip id -> path, one for each device served.")
        .def("__repr__", [](const SimulationConnector::Server &self) {
            return fmt::format(
                "SimulationConnector.Server(server_directory='{}', chips={})",
                self.server_directory().string(),
                self.sockets().size());
        });

    simulation_connector
        .def_static(
            "role_for",
            &SimulationConnector::role_for,
            nb::arg("simulator_directory"),
            release_gil(),
            "Host or client, decided from the path: a directory holding live simulation sockets means a host already "
            "serves there, so attach to it; anything else is a simulator build to host.")
        .def_static(
            "discover",
            [](const SimulationConnectorOptions &options) {
                SimulationConnector::Result result;
                {
                    nb::gil_scoped_release release;
                    result = SimulationConnector::discover(options);
                }
                if (result.server == nullptr) {
                    return nb::cast(std::make_tuple(
                        std::move(result.connection), std::move(result.devices), std::move(result.cluster_descriptor)));
                }
                // A served Result must stop serving before its devices go, which handing the devices to Python one
                // by one cannot guarantee. So the Result stays whole, owned by a capsule that every returned device
                // keeps alive: the last device dropped destroys it, server first.
                auto *served = new SimulationConnector::Result(std::move(result));
                nb::capsule owner(
                    served, [](void *owned) noexcept { delete static_cast<SimulationConnector::Result *>(owned); });
                nb::dict devices;
                for (const auto &[chip_id, device] : served->devices) {
                    nb::object py_device = nb::cast(device.get(), nb::rv_policy::reference);
                    nb::detail::keep_alive(py_device.ptr(), owner.ptr());
                    devices[nb::cast(chip_id)] = py_device;
                }
                return nb::cast(std::make_tuple(served->connection, devices, served->cluster_descriptor));
            },
            nb::arg("options"),
            "Opens the simulated devices for these options. Returns (connection, {chip_id: TTDevice}, "
            "cluster_descriptor) -- the connection says which role this process took and what simulator is behind "
            "it, and the cluster descriptor is the topology those devices sit in. With options.serve_over_sockets, "
            "the devices are served until every one of them is dropped.")
        .def_static(
            "serve",
            [](const nb::typed<nb::dict, ChipId, TTDevice> &devices,
               const ClusterDescriptor &cluster_descriptor,
               const std::filesystem::path &server_directory) {
                std::map<ChipId, TTDevice *> device_map;
                if (!nb::try_cast(devices, device_map)) {
                    throw nb::type_error("devices must map chip ids to TTDevice");
                }
                std::unique_ptr<SimulationConnector::Server> server;
                {
                    nb::gil_scoped_release release;
                    server = SimulationConnector::serve(device_map, cluster_descriptor, server_directory);
                }
                nb::typed<nb::object, SimulationConnector::Server> py_server = nb::cast(std::move(server));
                // Pins the device objects rather than the dict: the caller may empty the dict while serving
                // threads still use the devices. values() is a fresh list only this keep-alive holds.
                nb::detail::keep_alive(py_server.ptr(), devices.values().ptr());
                return py_server;
            },
            nb::arg("devices"),
            nb::arg("cluster_descriptor"),
            nb::arg("server_directory") = std::filesystem::path(),
            "Serves devices ({chip_id: TTDevice}, keyed by the chip id of cluster_descriptor each one is) to other "
            "processes, one per-chip socket each in one server directory, so a client (TopologyDiscovery or "
            "discover() pointed at that directory) can attach to them. Any TTDevice can be served, and stays usable "
            "while served. Client requests reach each device's read_from_device / write_to_device, one request per "
            "device at a time; GET_CLUSTER_DESCRIPTOR answers cluster_descriptor as it was at this call; SHUTDOWN is "
            "acknowledged and does nothing else. A device no simulator backs is described to clients as a TTSim "
            "device. server_directory is an existing directory to serve in; empty claims a fresh one. Returns the "
            "Server, which serves until dropped and keeps the devices it serves alive until then, even when they "
            "are removed from the devices dict. Raises "
            "error.UmdBaseException when devices is empty, when a chip id is not a chip of cluster_descriptor, "
            "when a live server already serves one of the chips in server_directory, or when UMD was built without "
            "simulation support.")
        .def_static(
            "allocate_server_directory",
            &SimulationConnector::allocate_server_directory,
            release_gil(),
            "Claims a fresh directory for one simulation server (the lowest free index) and returns it, so a caller "
            "can report the location before it starts serving there. The claim is atomic.")
        .def_static(
            "scan_servers",
            []() {
                SimulationConnector::ServerScan scan = SimulationConnector::scan_servers();
                return std::make_pair(std::move(scan.live), std::move(scan.removed));
            },
            release_gil(),
            "One pass over every server directory. Returns (live, removed) -- the servers still open, and the ones "
            "whose host was gone, which this removed. Both come from one scan, so a caller acting on both sees one "
            "consistent view.")
        .def_static(
            "list_servers",
            &SimulationConnector::list_servers,
            release_gil(),
            "The simulation servers currently open on this machine, ordered by index. A server whose host is gone "
            "is left out, and what it left on disk is cleared up.")
        .def_static(
            "prune_dead_servers",
            &SimulationConnector::prune_dead_servers,
            release_gil(),
            "The same sweep list_servers() does, reporting the other half: the servers whose host was gone, now "
            "removed. A directory that has published no socket yet is never swept.");
}

}  // namespace

void bind_simulation(nb::module_ &m) {
    nb::enum_<SimulationBackendType>(m, "SimulationBackendType")
        .value("TTSIM", SimulationBackendType::TTSIM, "The functional simulator (a libttsim .so).")
        .value("RTL", SimulationBackendType::RTL, "An RTL simulator build.");

    m.def(
        "get_simulator_arch",
        &get_simulator_arch,
        nb::arg("simulator_path"),
        release_gil(),
        "The architecture the simulator at simulator_path (a TTSim .so or an RTL simulator directory) models: the one "
        "its soc_descriptor.yaml declares, else the one its libttsim image reports. The first call for an image "
        "without a soc_descriptor.yaml has to come before any simulator runs in this process. Raises "
        "error.UmdBaseException if UMD was built without simulation support or the architecture cannot be told.");

    m.def(
        "find_simulator_soc_descriptor_path",
        &find_simulator_soc_descriptor_path,
        nb::arg("simulator_path"),
        "The soc_descriptor.yaml the simulator at simulator_path ships (beside a TTSim .so, or inside an RTL simulator "
        "directory), or an empty string when it ships none.");

    m.def(
        "get_simulation_backend",
        &get_simulation_backend,
        nb::arg("device"),
        "Which simulator backs device, whether it runs in this process or in a simulation server device is attached "
        "to; a remote device is backed by whatever backs the device it is reached through. None for a device no "
        "simulator backs, such as silicon reached through the KMD, and for every device when UMD was built without "
        "simulation support. A device attached to a simulation server that serves a device no simulator backs "
        "answers TTSIM, because the server protocol has no other kind.");

    bind_simulation_connector(m);
}
