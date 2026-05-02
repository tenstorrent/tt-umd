// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/set.h>

#include "umd/device/cluster.hpp"
#include "umd/device/topology/topology_discovery.hpp"

namespace nb = nanobind;

using namespace tt::umd;

void bind_cluster(nb::module_ &m) {
    nb::class_<Cluster>(m, "Cluster")
        .def(nb::init<>())
        .def("get_target_device_ids", &Cluster::get_target_device_ids)
        .def("get_clocks", &Cluster::get_clocks)
        // Explicitly close all devices (MMIO + remote) in the cluster.
        // Calling this before the Cluster object is destroyed ensures ETH
        // channels are torn down cleanly — the default Cluster::~Cluster()
        // does NOT call close_device(), leaving ETH in base-UMD firmware
        // state (0x49706550 sentinel) which contaminates subsequent callers.
        .def("close_device", &Cluster::close_device);
}
