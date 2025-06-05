/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/cluster.h"

#include <nanobind/nanobind.h>
// #include <nanobind/stl.h>  // Optional: For STL support

namespace nb = nanobind;

NB_MODULE(cluster_module, m) {
    // Assuming the class in cluster.h is named `Cluster`
    nb::class_<Cluster>(m, "Cluster")
        .def(nb::init<>())                                         // Bind the default constructor
        .def("method_name", &Cluster::method_name)                 // Bind a method
        .def_readwrite("property_name", &Cluster::property_name);  // Bind a property
}
