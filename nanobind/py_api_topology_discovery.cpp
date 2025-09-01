/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"

namespace nb = nanobind;

using namespace tt::umd;

void bind_topology_discovery(nb::module_ &m) {
    nb::class_<tt_ClusterDescriptor>(m, "ClusterDescriptor")
        .def("get_all_chips", &tt_ClusterDescriptor::get_all_chips)
        .def("is_chip_mmio_capable", &tt_ClusterDescriptor::is_chip_mmio_capable, nb::arg("chip_id"))
        .def("is_chip_remote", &tt_ClusterDescriptor::is_chip_remote, nb::arg("chip_id"))
        .def("get_closest_mmio_capable_chip", &tt_ClusterDescriptor::get_closest_mmio_capable_chip, nb::arg("chip"))
        .def("get_chips_local_first", &tt_ClusterDescriptor::get_chips_local_first, nb::arg("chips"))
        .def("get_chip_locations", &tt_ClusterDescriptor::get_chip_locations)
        .def("get_chips_with_mmio", &tt_ClusterDescriptor::get_chips_with_mmio)
        .def("get_active_eth_channels", &tt_ClusterDescriptor::get_active_eth_channels, nb::arg("chip_id"))
        .def("get_arch", &tt_ClusterDescriptor::get_arch, nb::arg("chip_id"));

    nb::class_<TopologyDiscovery>(m, "TopologyDiscovery")
        .def_static(
            "create_cluster_descriptor",
            [](std::unordered_set<chip_id_t> pci_target_devices = {}, std::string sdesc_path = "") {
                return TopologyDiscovery::create_cluster_descriptor(
                    std::move(pci_target_devices), std::move(sdesc_path));
            },
            nb::arg("pci_target_devices") = std::unordered_set<chip_id_t>{},
            nb::arg("sdesc_path") = "");
}
