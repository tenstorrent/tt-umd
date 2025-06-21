/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/cluster.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/set.h>

namespace nb = nanobind;

using namespace tt::umd;

NB_MODULE(libdevice, m) {
    nb::class_<Cluster>(m, "Cluster")
        .def(nb::init<>())
        .def("get_target_device_ids", &Cluster::get_target_device_ids)
        .def("get_clocks", &Cluster::get_clocks);
}
