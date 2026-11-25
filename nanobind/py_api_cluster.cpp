// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>

#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/topology/topology_discovery.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt::umd;

void bind_cluster(nb::module_ &m) {
    nb::class_<Cluster>(m, "Cluster")
        .def(nb::init<>(), release_gil())
        .def("get_target_device_ids", &Cluster::get_target_device_ids, release_gil())
        .def("get_clocks", &Cluster::get_clocks, release_gil())
        .def(
            "get_simulation_connection",
            &Cluster::get_simulation_connection,
            release_gil(),
            "Deprecated: get_simulation_backend(device) tells what backs a device, and the SimulationConnector.Server "
            "serving devices reports its directory and sockets. "
            "What simulation this cluster was constructed over (role, simulator, server directory), or None when "
            "it is not a simulation cluster. Its sockets are keyed by this cluster's chip ids. A host that stops "
            "serving afterwards does not change a client's answer.");
}
