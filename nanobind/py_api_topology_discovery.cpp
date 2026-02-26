// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

// Forward declaration of helper function from py_api_tt_device.cpp.
std::unique_ptr<TTDevice> create_remote_wormhole_tt_device(
    TTDevice* local_chip, ClusterDescriptor* cluster_descriptor, ChipId remote_chip_id);

void bind_topology_discovery(nb::module_& m) {
    nb::class_<ClusterDescriptor>(m, "ClusterDescriptor")
        .def_static("create_from_yaml_content", &ClusterDescriptor::create_from_yaml_content, nb::arg("yaml_content"))
        .def("get_all_chips", &ClusterDescriptor::get_all_chips)
        .def("is_chip_mmio_capable", &ClusterDescriptor::is_chip_mmio_capable, nb::arg("chip_id"))
        .def("is_chip_remote", &ClusterDescriptor::is_chip_remote, nb::arg("chip_id"))
        .def("get_closest_mmio_capable_chip", &ClusterDescriptor::get_closest_mmio_capable_chip, nb::arg("chip"))
        .def("get_chips_local_first", &ClusterDescriptor::get_chips_local_first, nb::arg("chips"))
        .def("get_chip_locations", &ClusterDescriptor::get_chip_locations)
        .def("get_chips_with_mmio", &ClusterDescriptor::get_chips_with_mmio)
        .def("get_active_eth_channels", &ClusterDescriptor::get_active_eth_channels, nb::arg("chip_id"))
        .def("get_ethernet_connections", &ClusterDescriptor::get_ethernet_connections)
        .def("get_chip_unique_ids", &ClusterDescriptor::get_chip_unique_ids)
        .def("get_io_device_type", &ClusterDescriptor::get_io_device_type)
        .def(
            "serialize_to_file",
            [](const ClusterDescriptor& self, const std::string& dest_file) -> std::string {
                std::filesystem::path file_path = self.serialize_to_file(dest_file);
                return file_path.string();
            },
            nb::arg("dest_file") = "")
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

    nb::class_<TopologyDiscoveryOptions>(m, "TopologyDiscoveryOptions")
        .def(nb::init<>())
        .def_rw("no_remote_discovery", &TopologyDiscoveryOptions::no_remote_discovery)
        .def_rw("no_wait_for_eth_training", &TopologyDiscoveryOptions::no_wait_for_eth_training)
        .def_rw("no_eth_firmware_strictness", &TopologyDiscoveryOptions::no_eth_firmware_strictness)
        .def_rw("predict_eth_fw_version", &TopologyDiscoveryOptions::predict_eth_fw_version)
        .def_rw("verify_eth_fw_hash", &TopologyDiscoveryOptions::verify_eth_fw_hash);

    nb::class_<TopologyDiscovery>(m, "TopologyDiscovery")
        .def_static(
            "create_cluster_descriptor",
            [](const TopologyDiscoveryOptions& options = {},
               IODeviceType io_device_type = IODeviceType::PCIe,
               const std::string& soc_descriptor_path = "") {
                return TopologyDiscovery::discover(options, io_device_type, soc_descriptor_path).first;
            },
            nb::arg("options") = TopologyDiscoveryOptions{},
            nb::arg("io_device_type") = IODeviceType::PCIe,
            nb::arg("soc_descriptor_path") = "")
        .def_static(
            "discover",
            &TopologyDiscovery::discover,
            nb::arg("options") = TopologyDiscoveryOptions{},
            nb::arg("io_device_type") = IODeviceType::PCIe,
            nb::arg("soc_descriptor_path") = "",
            "Discover topology and return both ClusterDescriptor and TTDevices");
}
