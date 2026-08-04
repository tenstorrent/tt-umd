// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt;
using namespace tt::umd;

// Forward declaration of helper function from py_api_tt_device.cpp.
std::unique_ptr<TTDevice> create_remote_wormhole_tt_device(
    TTDevice* local_chip, ClusterDescriptor* cluster_descriptor, ChipId remote_chip_id);

void bind_topology_discovery(nb::module_& m) {
    nb::class_<ClusterDescriptor>(m, "ClusterDescriptor")
        .def_static(
            "create_from_yaml_content",
            &ClusterDescriptor::create_from_yaml_content,
            nb::arg("yaml_content"),
            release_gil())
        .def("get_all_chips", &ClusterDescriptor::get_all_chips, release_gil())
        .def("is_chip_mmio_capable", &ClusterDescriptor::is_chip_mmio_capable, nb::arg("chip_id"), release_gil())
        .def("is_chip_remote", &ClusterDescriptor::is_chip_remote, nb::arg("chip_id"), release_gil())
        .def(
            "get_closest_mmio_capable_chip",
            &ClusterDescriptor::get_closest_mmio_capable_chip,
            nb::arg("chip"),
            release_gil())
        .def("get_chips_local_first", &ClusterDescriptor::get_chips_local_first, nb::arg("chips"), release_gil())
        .def("get_chip_locations", &ClusterDescriptor::get_chip_locations, release_gil())
        .def("get_chips_with_mmio", &ClusterDescriptor::get_chips_with_mmio, release_gil())
        .def("get_active_eth_channels", &ClusterDescriptor::get_active_eth_channels, nb::arg("chip_id"), release_gil())
        .def("get_ethernet_connections", &ClusterDescriptor::get_ethernet_connections, release_gil())
        .def("get_chip_unique_ids", &ClusterDescriptor::get_chip_unique_ids, release_gil())
        .def("get_io_device_type", &ClusterDescriptor::get_io_device_type, release_gil())
        .def(
            "serialize_to_file",
            [](const ClusterDescriptor& self, const std::string& dest_file) -> std::string {
                std::filesystem::path file_path = self.serialize_to_file(dest_file);
                return file_path.string();
            },
            nb::arg("dest_file") = "",
            release_gil())
        .def(
            "get_arch",
            static_cast<tt::ARCH (ClusterDescriptor::*)(ChipId) const>(&ClusterDescriptor::get_arch),
            nb::arg("chip_id"),
            release_gil())
        .def(
            "get_board_type",
            &ClusterDescriptor::get_board_type,
            nb::arg("chip_id"),
            release_gil(),
            "Get board type for a chip")
        .def(
            "get_board_id_for_chip",
            &ClusterDescriptor::get_board_id_for_chip,
            nb::arg("chip"),
            release_gil(),
            "Get board ID for a chip")
        .def(
            "get_tray_id",
            &ClusterDescriptor::get_tray_id,
            nb::arg("chip_id"),
            release_gil(),
            "Returns the tray id (1..4) for UBB boards, or None for non-UBB / unknown.")
        .def(
            "get_chip_pci_bdfs",
            &ClusterDescriptor::get_chip_pci_bdfs,
            release_gil(),
            "Map of ChipId -> PCI BDF string (e.g. \"0000:41:00.0\"). "
            "Only contains entries for MMIO-capable chips.")
        .def(
            "get_chip_to_bus_id",
            &ClusterDescriptor::get_chip_to_bus_id,
            release_gil(),
            "Map of ChipId -> PCI bus id (uint16, e.g. 0x41).")
        .def(
            "get_bus_id",
            &ClusterDescriptor::get_bus_id,
            nb::arg("chip_id"),
            release_gil(),
            "Returns the PCI bus id (uint16) for the given chip, or 0 if unknown.")
        .def(
            "get_asic_location",
            &ClusterDescriptor::get_asic_location,
            nb::arg("chip_id"),
            release_gil(),
            "Returns the ASIC location index within the chip's board (uint8), or 0 if unknown.")
        .def("get_unhealthy_devices", &ClusterDescriptor::get_unhealthy_devices, release_gil())
        .def("get_health_errors", &ClusterDescriptor::get_health_errors, release_gil())
        .def_static(
            "create_constrained_cluster_descriptor",
            [](const ClusterDescriptor& full_cluster_desc, const std::unordered_set<ChipId>& target_chip_ids) {
                return ClusterDescriptor::create_constrained_cluster_descriptor(&full_cluster_desc, target_chip_ids);
            },
            nb::arg("full_cluster_desc"),
            nb::arg("target_chip_ids") = std::unordered_set<ChipId>{},
            "Create a constrained cluster descriptor filtered to the given chip IDs");

    nb::class_<TopologyDiscoveryOptions> topology_discovery_options(m, "TopologyDiscoveryOptions");

    nb::enum_<TopologyDiscoveryOptions::Action>(topology_discovery_options, "Action")
        .value("THROW", TopologyDiscoveryOptions::Action::THROW)
        .value("IGNORE", TopologyDiscoveryOptions::Action::IGNORE);

    topology_discovery_options.def(nb::init<>(), release_gil())
        .def_rw("cmfw_mismatch_action", &TopologyDiscoveryOptions::cmfw_mismatch_action)
        .def_rw("cmfw_unsupported_action", &TopologyDiscoveryOptions::cmfw_unsupported_action)
        .def_rw("eth_fw_mismatch_action", &TopologyDiscoveryOptions::eth_fw_mismatch_action)
        .def_rw("unexpected_routing_firmware_config", &TopologyDiscoveryOptions::unexpected_routing_firmware_config)
        .def_rw("eth_fw_heartbeat_failure", &TopologyDiscoveryOptions::eth_fw_heartbeat_failure)
        .def_rw("device_init_failure_action", &TopologyDiscoveryOptions::device_init_failure_action)
        .def_rw("discover_remote_devices", &TopologyDiscoveryOptions::discover_remote_devices)
        .def_rw("wait_on_ethernet_link_training", &TopologyDiscoveryOptions::wait_on_ethernet_link_training)
        .def_rw("perform_6u_eth_retrain", &TopologyDiscoveryOptions::perform_6u_eth_retrain)
        .def_rw("low_power", &TopologyDiscoveryOptions::low_power)
        .def_rw("use_safe_api", &TopologyDiscoveryOptions::use_safe_api);

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
            nb::arg("soc_descriptor_path") = "",
            release_gil())
        .def_static(
            "discover",
            &TopologyDiscovery::discover,
            nb::arg("options") = TopologyDiscoveryOptions{},
            nb::arg("io_device_type") = IODeviceType::PCIe,
            nb::arg("soc_descriptor_path") = "",
            release_gil(),
            "Discover topology and return both ClusterDescriptor and TTDevices");
}
