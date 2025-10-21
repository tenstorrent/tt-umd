/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

// Helper function for easy creation of RemoteWormholeTTDevice
std::unique_ptr<TTDevice> create_remote_wormhole_tt_device(
    TTDevice *local_chip, ClusterDescriptor *cluster_descriptor, ChipId remote_chip_id) {
    // Note: this chip id has to match the local_chip passed. Figure out if there's a better way to do this.
    ChipId local_chip_id = cluster_descriptor->get_closest_mmio_capable_chip(remote_chip_id);
    EthCoord target_chip = cluster_descriptor->get_chip_locations().at(remote_chip_id);
    SocDescriptor local_soc_descriptor = SocDescriptor(local_chip->get_arch(), local_chip->get_chip_info());
    auto remote_communication = RemoteCommunication::create_remote_communication(local_chip, target_chip);
    remote_communication->set_remote_transfer_ethernet_cores(
        local_soc_descriptor.get_eth_xy_pairs_for_channels(cluster_descriptor->get_active_eth_channels(local_chip_id)));
    return TTDevice::create(std::move(remote_communication));
}

void bind_tt_device(nb::module_ &m) {
    nb::enum_<IODeviceType>(m, "IODeviceType").value("PCIe", IODeviceType::PCIe).value("JTAG", IODeviceType::JTAG);

    nb::class_<PciDeviceInfo>(m, "PciDeviceInfo")
        .def_ro("pci_domain", &PciDeviceInfo::pci_domain)
        .def_ro("pci_bus", &PciDeviceInfo::pci_bus)
        .def_ro("pci_device", &PciDeviceInfo::pci_device)
        .def_ro("pci_function", &PciDeviceInfo::pci_function)
        .def_ro("pci_bdf", &PciDeviceInfo::pci_bdf)
        .def("get_arch", &PciDeviceInfo::get_arch);

    nb::class_<PCIDevice>(m, "PCIDevice")
        .def(nb::init<int>())
        .def_static(
            "enumerate_devices",
            [](std::unordered_set<int> pci_target_devices = {}) {
                return PCIDevice::enumerate_devices(pci_target_devices);
            },
            nb::arg("pci_target_devices") = std::unordered_set<int>{},
            "Enumerates PCI devices, optionally filtering by target devices.")
        .def_static(
            "enumerate_devices_info",
            [](std::unordered_set<int> pci_target_devices = {}) {
                return PCIDevice::enumerate_devices_info(pci_target_devices);
            },
            nb::arg("pci_target_devices") = std::unordered_set<int>{},
            "Enumerates PCI device information, optionally filtering by target devices.")
        .def("get_device_info", &PCIDevice::get_device_info);

    nb::class_<TTDevice>(m, "TTDevice")
        .def_static(
            "create",
            static_cast<std::unique_ptr<TTDevice> (*)(int, IODeviceType)>(&TTDevice::create),
            nb::arg("device_number"),
            nb::arg("device_type") = IODeviceType::PCIe,
            nb::rv_policy::take_ownership)
        .def("init_tt_device", &TTDevice::init_tt_device)
        .def("get_arc_telemetry_reader", &TTDevice::get_arc_telemetry_reader, nb::rv_policy::reference_internal)
        .def("get_arch", &TTDevice::get_arch)
        .def("get_board_id", &TTDevice::get_board_id)
        .def("get_board_type", &TTDevice::get_board_type)
        .def("get_pci_device", &TTDevice::get_pci_device, nb::rv_policy::reference)
        .def(
            "noc_read32",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr) -> uint32_t {
                tt_xy_pair core = {core_x, core_y};
                uint32_t value = 0;
                self.read_from_device(&value, core, addr, sizeof(uint32_t));
                return value;
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"));

    nb::class_<RemoteWormholeTTDevice, TTDevice>(m, "RemoteWormholeTTDevice");

    // SmBusArcTelemetryReader binding - for direct instantiation when SMBUS telemetry is needed
    nb::class_<SmBusArcTelemetryReader, ArcTelemetryReader>(m, "SmBusArcTelemetryReader")
        .def(nb::init<TTDevice *>(), nb::arg("tt_device"))
        .def("read_entry", &SmBusArcTelemetryReader::read_entry, nb::arg("telemetry_tag"))
        .def("is_entry_available", &SmBusArcTelemetryReader::is_entry_available, nb::arg("telemetry_tag"));

    m.def(
        "create_remote_wormhole_tt_device",
        &create_remote_wormhole_tt_device,
        nb::arg("local_chip"),
        nb::arg("cluster_descriptor"),
        nb::arg("remote_chip_id"),
        nb::rv_policy::take_ownership,
        "Creates a RemoteWormholeTTDevice for communication with a remote chip.");
}
