/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

// Forward declaration of helper function from py_api_tt_device.cpp
std::unique_ptr<TTDevice> create_remote_wormhole_tt_device(
    TTDevice* local_chip, ClusterDescriptor* cluster_descriptor, ChipId remote_chip_id);

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
            nb::arg("sdesc_path") = "")
        .def_static(
            "discover",
            [](std::unordered_set<ChipId> pci_target_devices = {}, const std::string& sdesc_path = "") {
                TopologyDiscoveryOptions options;
                options.soc_descriptor_path = sdesc_path;
                auto [cluster_desc, chips] = TopologyDiscovery::discover(options);

                // Note that we have to create mmio chips first, since they are passed to the construction of the remote
                // chips.
                std::vector<ChipId> chips_to_construct =
                    cluster_desc->get_chips_local_first(cluster_desc->get_all_chips());
                std::map<ChipId, std::unique_ptr<TTDevice>> tt_devices;

                for (ChipId chip_id : chips_to_construct) {
                    if (cluster_desc->is_chip_mmio_capable(chip_id)) {
                        auto chip_to_mmio_map = cluster_desc->get_chips_with_mmio();
                        int pci_device_num = chip_to_mmio_map.at(chip_id);
                        tt_devices[chip_id] = TTDevice::create(pci_device_num);
                    } else {
                        ChipId closest_mmio = cluster_desc->get_closest_mmio_capable_chip(chip_id);
                        tt_devices[chip_id] = create_remote_wormhole_tt_device(
                            tt_devices[closest_mmio].get(), cluster_desc.get(), chip_id);
                    }
                    tt_devices[chip_id]->init_tt_device();
                }

                return std::make_pair(std::move(cluster_desc), std::move(tt_devices));
            },
            nb::arg("pci_target_devices") = std::unordered_set<ChipId>{},
            nb::arg("sdesc_path") = "",
            "Discover topology and return both ClusterDescriptor and TTDevices");
}
