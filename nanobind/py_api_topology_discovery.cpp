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

using namespace tt;
using namespace tt::umd;

void bind_topology_discovery(nb::module_& m) {
    nb::class_<ClusterDescriptor>(m, "ClusterDescriptor")
        .def("get_all_chips", &ClusterDescriptor::get_all_chips)
        .def("is_chip_mmio_capable", &ClusterDescriptor::is_chip_mmio_capable, nb::arg("chip_id"))
        .def("is_chip_remote", &ClusterDescriptor::is_chip_remote, nb::arg("chip_id"))
        .def("get_closest_mmio_capable_chip", &ClusterDescriptor::get_closest_mmio_capable_chip, nb::arg("chip"))
        .def("get_chips_local_first", &ClusterDescriptor::get_chips_local_first, nb::arg("chips"))
        .def("get_chip_locations", &ClusterDescriptor::get_chip_locations)
        .def("get_chips_with_mmio", &ClusterDescriptor::get_chips_with_mmio)
        .def("get_active_eth_channels", &ClusterDescriptor::get_active_eth_channels, nb::arg("chip_id"))
        .def(
            "get_arch",
            static_cast<tt::ARCH (ClusterDescriptor::*)(ChipId) const>(&ClusterDescriptor::get_arch),
            nb::arg("chip_id"))
        .def("get_board_type", &ClusterDescriptor::get_board_type, nb::arg("chip_id"), "Get board type for a chip")
        .def(
            "get_board_id_for_chip",
            &ClusterDescriptor::get_board_id_for_chip,
            nb::arg("chip"),
            "Get board ID for a chip");

    nb::class_<TopologyDiscovery>(m, "TopologyDiscovery")
        .def_static(
            "create_cluster_descriptor",
            [](std::unordered_set<ChipId> pci_target_devices = {}, const std::string& sdesc_path = "") {
                TopologyDiscoveryOptions options;
                options.soc_descriptor_path = sdesc_path;
                return TopologyDiscovery::discover(options).first;
            },
            nb::arg("pci_target_devices") = std::unordered_set<ChipId>{},
            nb::arg("sdesc_path") = "");
}
