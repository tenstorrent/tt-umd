/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/cluster.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

namespace nb = nanobind;

using namespace tt::umd;

NB_MODULE(tt_umd, m) {
    // Expose the Cluster class
    nb::class_<Cluster>(m, "Cluster")
        .def(nb::init<>())
        .def("get_target_device_ids", &Cluster::get_target_device_ids)
        .def("get_clocks", &Cluster::get_clocks);

    // Expose the ArcTelemetryReader class
    nb::class_<ArcTelemetryReader>(m, "ArcTelemetryReader")
        .def("read_entry", &ArcTelemetryReader::read_entry, nb::arg("telemetry_tag"))
        .def("is_entry_available", &ArcTelemetryReader::is_entry_available, nb::arg("telemetry_tag"));

    // Expose the TTDevice class
    nb::class_<TTDevice>(m, "TTDevice")
        .def_static("create", &TTDevice::create, nb::arg("pci_device_number"), nb::rv_policy::take_ownership)
        .def("get_arc_telemetry_reader", &TTDevice::get_arc_telemetry_reader, nb::rv_policy::reference_internal);

    // Expose the PCIDevice class
    nb::class_<PCIDevice>(m, "PCIDevice")
        .def(nb::init<int>())
        // std::vector<int> PCIDevice::enumerate_devices() {
        .def("enumerate_devices", &PCIDevice::enumerate_devices)
        .def("enumerate_devices_info", &PCIDevice::enumerate_devices_info);
}
