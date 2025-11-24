// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
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
        .def("get_clocks", &Cluster::get_clocks);
}
