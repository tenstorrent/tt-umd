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
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/simulation/simulation_connector.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt::umd;

#ifdef TT_UMD_BUILD_SIMULATION

void bind_simulation(nb::module_ &m) {
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
            "Host path only: publish each simulated chip over a per-chip socket so other processes can attach as "
            "clients. Off by default, so discover() yields private in-process devices.")
        .def_rw(
            "server_directory",
            &SimulationConnectorOptions::server_directory,
            "Host path only: the directory to serve the per-chip sockets in. Empty means allocate a fresh one.")
        .def_rw(
            "cluster_descriptor",
            &SimulationConnectorOptions::cluster_descriptor,
            "Connectivity/topology used to configure the simulator on the host path. Optional; a client takes the "
            "topology from the host instead.")
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

    nb::enum_<SimulationBackendType>(m, "SimulationBackendType")
        .value("TTSIM", SimulationBackendType::TTSIM, "The functional simulator (a libttsim .so).")
        .value("RTL", SimulationBackendType::RTL, "An RTL simulator build.");

    nb::class_<SimulationConnector> simulation_connector(
        m,
        "SimulationConnector",
        "Entry point for opening simulated devices, mirroring silicon TopologyDiscovery. The role is decided purely "
        "from what simulator_directory points at.");

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
                SimulationConnector::Result result = SimulationConnector::discover(options);
                return std::make_pair(std::move(result.connection), std::move(result.devices));
            },
            nb::arg("options"),
            release_gil(),
            "Opens the simulated devices for these options. Returns (connection, {chip_id: TTDevice}) -- the "
            "connection says which role this process took and what simulator is behind it.")
        .def_static(
            "allocate_server_directory",
            &SimulationConnector::allocate_server_directory,
            release_gil(),
            "Claims a fresh directory for one simulation server (the lowest free index) and returns it, so a caller "
            "can report the location before it starts serving there. The claim is atomic.")
        .def_static(
            "list_servers",
            &SimulationConnector::list_servers,
            release_gil(),
            "The simulation servers currently open on this machine, ordered by index. Does not connect to them.");
}

#else

// Simulation support is not built; the module exposes no simulation API.
void bind_simulation(nb::module_ &) {}

#endif
